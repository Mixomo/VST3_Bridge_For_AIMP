#include <windows.h>
#include <memory>
#include <mutex>
#include <cstdio>
#include "BridgeRuntime.h"

#ifndef AIMP_VST3_BRIDGE_ENABLE_AIMP_PLUGIN
#define AIMP_VST3_BRIDGE_ENABLE_AIMP_PLUGIN 1
#endif

#ifndef AIMP_VST3_BRIDGE_ENABLE_WINAMP_DSP
#define AIMP_VST3_BRIDGE_ENABLE_WINAMP_DSP 1
#endif

#ifndef AIMP_VST3_BRIDGE_OUT_OF_PROCESS
#define AIMP_VST3_BRIDGE_OUT_OF_PROCESS 0
#endif

// ── AIMP Native SDK ──────────────────────────────────────────
#if AIMP_VST3_BRIDGE_ENABLE_AIMP_PLUGIN
#include "apiTypes.h"
#include "apiObjects.h"
#include "apiCore.h"
#include "apiPlugin.h"
#endif

#if AIMP_VST3_BRIDGE_ENABLE_WINAMP_DSP
#include "winamp_dsp.h"
#endif
#if AIMP_VST3_BRIDGE_OUT_OF_PROCESS
#include "OutProcClient.h"
#else
#include "VST3HostEngine.h"
#include "HostWindow.h"
#endif

// ── Module-level HINSTANCE (safe: no constructor/destructor) ─
static HINSTANCE g_hInst = nullptr;

namespace
{
#if AIMP_VST3_BRIDGE_OUT_OF_PROCESS
    std::mutex g_runtimeMutex;
    int g_runtimeRefs = 0;
    OutProcClient g_outProcClient;

    void WriteDebugLog(const char* message)
    {
        wchar_t modulePath[32768] {};
        GetModuleFileNameW(g_hInst, modulePath, static_cast<DWORD>(std::size(modulePath)));
        const auto logPath = bridge::RuntimePaths::detect(juce::File(juce::String(modulePath))).logFile;
        FILE* file = nullptr;
        if (_wfopen_s(&file, logPath.getFullPathName().toWideCharPointer(), L"ab") != 0 || file == nullptr)
            return;

        SYSTEMTIME st = {};
        GetLocalTime(&st);
        std::fprintf(file, "%04u-%02u-%02u %02u:%02u:%02u.%03u %s\r\n",
                     st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, message);
        std::fclose(file);
    }

    bool StartRuntime()
    {
        std::lock_guard<std::mutex> lock(g_runtimeMutex);
        if (g_runtimeRefs > 0 && g_outProcClient.isRunning())
            return true;

        if (g_runtimeRefs > 0)
            g_outProcClient.stop();

        if (!g_outProcClient.start(g_hInst))
        {
            g_runtimeRefs = 0;
            WriteDebugLog("Failed to start out-of-process VST3 host");
            return false;
        }

        g_runtimeRefs = 1;
        WriteDebugLog("Out-of-process VST3 host started");
        return true;
    }

    void StopRuntime()
    {
        std::lock_guard<std::mutex> lock(g_runtimeMutex);
        if (g_runtimeRefs <= 0)
            return;
        g_runtimeRefs = 0;
        char stats[512] = {};
        sprintf_s(stats,
                  "Out-of-process pipeline stats: submitted=%llu completed=%llu missed=%llu stale=%llu queueFull=%llu emergencyBypass=%llu clientMaxMicros=%llu clientOver1ms=%llu clientOver3ms=%llu",
                  static_cast<unsigned long long>(g_outProcClient.getSubmittedBlocks()),
                  static_cast<unsigned long long>(g_outProcClient.getCompletedBlocks()),
                  static_cast<unsigned long long>(g_outProcClient.getMissedBlocks()),
                  static_cast<unsigned long long>(g_outProcClient.getStaleBlocks()),
                  static_cast<unsigned long long>(g_outProcClient.getQueueFullBlocks()),
                  static_cast<unsigned long long>(g_outProcClient.getEmergencyBypassBlocks()),
                  static_cast<unsigned long long>(g_outProcClient.getMaxClientCallbackMicros()),
                  static_cast<unsigned long long>(g_outProcClient.getClientCallbacksOver1ms()),
                  static_cast<unsigned long long>(g_outProcClient.getClientCallbacksOver3ms()));
        WriteDebugLog(stats);
        g_outProcClient.stop();
        WriteDebugLog("Out-of-process VST3 host stopped");
    }

    bool IsRuntimeStarted()
    {
        std::lock_guard<std::mutex> lock(g_runtimeMutex);
        return g_runtimeRefs > 0 && g_outProcClient.isRunning();
    }

    void ShowHostConfig()
    {
        if (!IsRuntimeStarted() && !StartRuntime())
            return;
        WriteDebugLog(g_outProcClient.showEditor() ? "Show GUI request queued" : "Show GUI request failed");
    }

#else
    std::mutex g_runtimeMutex;
    int g_runtimeRefs = 0;
    bool g_juceInitialised = false;
    std::unique_ptr<HostWindow> g_hostWindow;

    void WriteDebugLog(const char* message)
    {
        wchar_t modulePath[32768] {};
        GetModuleFileNameW(g_hInst, modulePath, static_cast<DWORD>(std::size(modulePath)));
        const auto logPath = bridge::RuntimePaths::detect(juce::File(juce::String(modulePath))).logFile;
        SYSTEMTIME st = {};
        GetLocalTime(&st);

        FILE* file = nullptr;
        if (_wfopen_s(&file, logPath.getFullPathName().toWideCharPointer(), L"ab") != 0 || file == nullptr)
            return;

        std::fprintf(file, "%04u-%02u-%02u %02u:%02u:%02u.%03u %s\r\n",
                     st.wYear, st.wMonth, st.wDay,
                     st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                     message);
        std::fclose(file);
    }

    void WriteDebugLogF(const char* format, int a, int b, int c, int d)
    {
        char buffer[256] = {};
        sprintf_s(buffer, format, a, b, c, d);
        WriteDebugLog(buffer);
    }

    bool StartRuntime()
    {
        WriteDebugLog("StartRuntime requested");
        std::lock_guard<std::mutex> lock(g_runtimeMutex);

        if (g_runtimeRefs++ > 0)
        {
            WriteDebugLog("StartRuntime reused existing runtime");
            return true;
        }

        try
        {
            juce::initialiseJuce_GUI();
            g_juceInitialised = true;

            VST3HostEngine::createInstance();
            VST3HostEngine::getInstance().init(g_hInst);
            WriteDebugLog("StartRuntime success");
            return true;
        }
        catch (...)
        {
            WriteDebugLog("StartRuntime failed with exception");
            VST3HostEngine::destroyInstance();

            if (g_juceInitialised)
            {
                juce::shutdownJuce_GUI();
                g_juceInitialised = false;
            }

            g_runtimeRefs = 0;
            return false;
        }
    }

    void StopRuntime()
    {
        WriteDebugLog("StopRuntime requested");
        std::lock_guard<std::mutex> lock(g_runtimeMutex);

        if (g_runtimeRefs <= 0)
            return;

        if (--g_runtimeRefs > 0)
        {
            WriteDebugLog("StopRuntime kept runtime alive");
            return;
        }

        if (g_hostWindow)
        {
            g_hostWindow->setVisible(false);
            g_hostWindow.reset();
        }

        VST3HostEngine::destroyInstance();

        if (g_juceInitialised)
        {
            juce::shutdownJuce_GUI();
            g_juceInitialised = false;
        }
        WriteDebugLog("StopRuntime complete");
    }

    bool IsRuntimeStarted()
    {
        std::lock_guard<std::mutex> lock(g_runtimeMutex);
        return g_runtimeRefs > 0;
    }

    void ShowHostConfig()
    {
        if (!IsRuntimeStarted())
        {
            if (!StartRuntime())
                return;
        }

        if (g_hostWindow)
        {
            g_hostWindow->setVisible(true);
            g_hostWindow->toFront(true);
        }
        else
        {
            g_hostWindow = std::make_unique<HostWindow>();
        }
    }
#endif
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_hInst = hModule;
        WriteDebugLog("DllMain attach");
        // Prevent per-thread DllMain calls — reduces risk of
        // teardown races when JUCE threads are running.
        DisableThreadLibraryCalls(hModule);
    }
    // DO NOT touch any JUCE or C++ objects here.
    // All lifecycle work happens in Initialize() / Finalize().
    return TRUE;
}

// ── IAIMPPlugin implementation ────────────────────────────────
#if AIMP_VST3_BRIDGE_ENABLE_AIMP_PLUGIN
//
// All JUCE state lives as class members so their destructors run
// deterministically inside Finalize(), NOT at DLL_PROCESS_DETACH
// (which is too late — other DLLs may already be gone).
//
class AIMPVst3Plugin : public IAIMPPlugin,
                       public IAIMPExternalSettingsDialog
{
public:
    AIMPVst3Plugin()  = default;
    ~AIMPVst3Plugin() = default;

    // ── IUnknown ─────────────────────────────────────────────
    ULONG WINAPI AddRef() override  { return ++m_refCount; }
    ULONG WINAPI Release() override
    {
        ULONG rc = --m_refCount;
        if (rc == 0) delete this;
        return rc;
    }
    HRESULT WINAPI QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown)
        {
            *ppv = static_cast<IAIMPPlugin*>(this);
            AddRef();
            return S_OK;
        }
        if (riid == IID_IAIMPExternalSettingsDialog)
        {
            *ppv = static_cast<IAIMPExternalSettingsDialog*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    // ── IAIMPPlugin info ─────────────────────────────────────
    TChar* WINAPI InfoGet(int Index) override
    {
        switch (Index)
        {
        case AIMP_PLUGIN_INFO_NAME:
            return const_cast<TChar*>(L"VST3 Bridge DSP");
        case AIMP_PLUGIN_INFO_AUTHOR:
            return const_cast<TChar*>(L"Ezequiel Casas (https://github.com/Mixomo)");
        case AIMP_PLUGIN_INFO_SHORT_DESCRIPTION:
            return const_cast<TChar*>(L"Hosts VST3 plug-ins as AIMP DSP effects");
        case AIMP_PLUGIN_INFO_FULL_DESCRIPTION:
            return const_cast<TChar*>(
                L"Hosts VST3 plug-ins as AIMP DSP effects "
                L"via the JUCE AudioPluginHost engine. "
                L"Developed by Ezequiel Casas (https://github.com/Mixomo).");
        default:
            return nullptr;
        }
    }

    LongWord WINAPI InfoGetCategories() override
    {
        return AIMP_PLUGIN_CATEGORY_ADDONS;
    }

    // ── Lifecycle ────────────────────────────────────────────
    HRESULT WINAPI Initialize(IAIMPCore* Core) override
    {
        if (!Core) return E_POINTER;
        m_core = Core;
        m_core->AddRef();

        return S_OK;
    }

    HRESULT WINAPI Finalize() override
    {
        if (m_runtimeStarted)
            StopRuntime();
        m_runtimeStarted = false;

        if (m_core)
        {
            m_core->Release();
            m_core = nullptr;
        }

        return S_OK;
    }

    // ── System notifications ──────────────────────────────────
    void WINAPI SystemNotification(int /*NotifyID*/, IUnknown* /*Data*/) override {}

    // ── Optional settings dialog queried by AIMP ──────────────
    void WINAPI Show(HWND /*ParentWindow*/) override
    {
        ShowConfigDialog();
    }

    // ── Config dialog (called from AIMP plugin manager) ───────
    void ShowConfigDialog()
    {
        if (!m_runtimeStarted)
            m_runtimeStarted = StartRuntime();

        ShowHostConfig();
    }

private:
    IAIMPCore* m_core = nullptr;
    ULONG m_refCount = 1;   // caller must Release()
    bool m_runtimeStarted = false;
};
#endif

// ── The export AIMP actually looks for ───────────────────────
#if AIMP_VST3_BRIDGE_ENABLE_AIMP_PLUGIN
extern "C" __declspec(dllexport)
HRESULT WINAPI AIMPPluginGetHeader(IAIMPPlugin** Header)
{
    if (!Header) return E_POINTER;
    *Header = new AIMPVst3Plugin();   // refcount starts at 1
    return S_OK;
}
#endif

// ── Winamp DSP API, used by AIMP's DSP pipeline ──────────────
#if AIMP_VST3_BRIDGE_ENABLE_WINAMP_DSP
//
// AIMP can load Winamp-style DSP modules named dsp_*.dll. This is the actual
// audio processing entry point: Init starts the shared VST3 runtime,
// ModifySamples feeds interleaved PCM into JUCE, and Quit tears it down.
//
static void __cdecl DspConfig(winampDSPModule*)
{
    WriteDebugLog("DspConfig called");
    ShowHostConfig();
}

static int __cdecl DspInit(winampDSPModule* thisMod)
{
    WriteDebugLog("DspInit called");
    if (thisMod == nullptr)
        return 1;

    if (!StartRuntime())
        return 1;

#if AIMP_VST3_BRIDGE_OUT_OF_PROCESS
    g_outProcClient.notifyDspStarted();
#endif

    thisMod->userData = reinterpret_cast<void*>(1);
    return 0;
}

static int __cdecl DspModifySamples(winampDSPModule* thisMod,
                                    short int* samples,
                                    int numSamples,
                                    int bps,
                                    int numChannels,
                                    int sampleRate)
{
    if (thisMod == nullptr || samples == nullptr || numSamples <= 0)
        return numSamples;

    if (numChannels <= 0)
        return numSamples;

    if (thisMod->userData == nullptr)
        return numSamples;

    try
    {
#if AIMP_VST3_BRIDGE_OUT_OF_PROCESS
        g_outProcClient.process(samples, numSamples, bps, numChannels, sampleRate);
#else
        VST3HostEngine::getInstance().processAudio(
            samples, numSamples, bps, numChannels, sampleRate);

        if (VST3HostEngine::getInstance().consumeBypassRequested())
        {
            WriteDebugLog("DSP bridge requested bypass; stopping runtime");
            if (thisMod != nullptr)
                thisMod->userData = nullptr;
            StopRuntime();
        }
#endif

        return numSamples;
    }
    catch (...)
    {
        WriteDebugLog("DspModifySamples failed with exception");
        return numSamples;
    }
}

static void __cdecl DspQuit(winampDSPModule* thisMod)
{
    WriteDebugLog("DspQuit called");
    if (thisMod == nullptr || thisMod->userData == nullptr)
        return;

    thisMod->userData = nullptr;
    StopRuntime();
}

static char g_dspModuleDescription[] = "VST3 Bridge - Mixomo";
static char g_dspHeaderDescription[] = "VST3 Bridge - Mixomo";

static winampDSPModule g_dspModule = {
    g_dspModuleDescription,
    nullptr,
    nullptr,
    DspConfig,
    DspInit,
    DspModifySamples,
    DspQuit,
    nullptr
};

static winampDSPModule* __cdecl DspGetModule(int num)
{
    if (num != 0)
        return nullptr;

    g_dspModule.hDllInstance = g_hInst;
    return &g_dspModule;
}

static winampDSPHeader g_dspHeader = {
    DSP_HDRVER,
    g_dspHeaderDescription,
    DspGetModule
};

static int __cdecl DspHeaderSanity(int v)
{
    int res = v * static_cast<int>(1103515245UL);
    res += static_cast<int>(13293UL);
    res &= static_cast<int>(0x7FFFFFFFUL);
    res ^= v;
    return res;
}

static winampDSPHeaderEx g_dspHeaderEx = {
    DSP_HDRVER + 1,
    g_dspHeaderDescription,
    DspGetModule,
    DspHeaderSanity
};

extern "C" __declspec(dllexport)
winampDSPHeader* __cdecl winampDSPGetHeader()
{
    WriteDebugLog("winampDSPGetHeader called");
    return &g_dspHeader;
}

extern "C" __declspec(dllexport)
winampDSPHeaderEx* __cdecl winampDSPGetHeader2()
{
    WriteDebugLog("winampDSPGetHeader2 called");
    return &g_dspHeaderEx;
}
#endif
