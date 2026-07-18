#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_data_structures/juce_data_structures.h>
#include <atomic>
#include <functional>
#include <vector>

#include "BridgeRuntime.h"

class VST3HostEngine : public juce::AudioProcessorListener
{
public:
    static VST3HostEngine& getInstance();

    void init(void* hInst, const bridge::RuntimePaths& paths, bool restorePlugin = true);
    void shutdown();

    juce::Array<juce::PluginDescription> getAvailablePlugins() const;
    
    bool loadPlugin(const juce::String& path, bool restoreSavedState = true);
    bool loadPluginFromFile(const juce::File& pluginFile);
    void unloadPlugin();
    bool cloneRackSlot(int index, int insertAt);
    void removeRackSlot(int index);
    void clearRack();
    void moveRackSlot(int from, int to);
    void selectRackSlot(int index);
    int getSelectedRackSlot() const { return selectedRackSlot; }
    int getRackSize() const { return static_cast<int>(rackInstances.size()); }
    juce::StringArray getRackPluginPaths() const;
    juce::AudioPluginInstance* getRackPluginInstance(int index) const;
    bool isRackSlotMuted(int index) const;
    bool isRackSlotSolo(int index) const;
    int getRackSlotEditorHeight(int index) const;
    void setRackSlotMuted(int index, bool muted);
    void setRackSlotSolo(int index, bool solo);
    void setRackSlotEditorHeight(int index, int height);
    
    int processAudio(void* samples, int numFrames, int bps, int numChannels, int sampleRate);
    
    bool hasActivePlugin() const { return !rackInstances.empty(); }
    juce::AudioPluginInstance* getPluginInstance() const { return pluginInstance; }
    juce::String getActivePluginPath() const { return activePluginPath; }
    bool hasPendingPluginState() const { return !pendingPluginState.isEmpty(); }
    juce::uint32 getEditorStateRevision() const { return editorStateRevision.load(); }

    void saveState();
    void flushPendingStateSave();
    void loadState(bool restorePlugin = true);
    void applyPendingPluginState();
    juce::String getStartupWarning() const { return startupWarning; }
    void acceptExplicitBypass() { preserveRecoveredState = false; }
    void rememberPluginDescription(const juce::PluginDescription& desc);
    bool isPluginIncompatible(const juce::String& path) const;
    bool consumeBypassRequested();
    void quarantineFailedPlugin(const juce::String& path);
    void markPluginLoadFailure(const juce::String& path, const juce::String& error);
    void removePluginFromList(const juce::String& path);
    void scanAll(bool force = false);
    void scanFolder(const bridge::ScanFolder& folder, bool force = true);
    void scanFileAndLoad(const juce::File& file);
    void cancelScan();
    bool isScanning() const;
    juce::String getScanProgress() const;
    bridge::BridgeSettings getSettingsSnapshot() const;
    bridge::RuntimePaths getRuntimePaths() const;
    void updateSettings(const bridge::BridgeSettings& newSettings);
    juce::Array<bridge::PluginRecord> getPluginRecords() const;
    void retryPlugin(const juce::String& canonicalPath);
    void resetScanCache();
    void resetActivePluginParameters();
    void forgetAllPlugins();
    juce::String getDiagnostics() const;
    void setHostArchitectureRequester(std::function<void(bridge::Architecture)> callback);
    void requestHostArchitecture(const bridge::PluginRecord& plugin);

    // AudioProcessorListener callbacks
    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override;
    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override;

    // Lifecycle: call createInstance() before any use, destroyInstance()
    // before juce::shutdownJuce_GUI(). Using explicit lifetime instead of a
    // Meyer's-singleton static so the destructor runs in Finalize() (while
    // JUCE is alive), NOT at DLL_PROCESS_DETACH (when JUCE is long gone).
    static void createInstance();
    static void destroyInstance();

    VST3HostEngine();
    ~VST3HostEngine() override;

    bool preparePlugin(double sampleRate, int numChannels, int blockSize);
    juce::File getConfigFile() const;
    juce::File getLogFile() const;
    void writeLog(const juce::String& message) const;

    void* dllInstance = nullptr;
    bridge::RuntimePaths runtimePaths;
    std::unique_ptr<bridge::ConfigStore> configStore;
    bridge::BridgeSettings settings;
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList pluginList;
    
    struct RackInstance
    {
        juce::String path;
        std::unique_ptr<juce::AudioPluginInstance> plugin;
        bool muted = false;
        bool solo = false;
        int editorHeight = 0;
    };
    std::vector<RackInstance> rackInstances;
    juce::AudioPluginInstance* pluginInstance = nullptr;
    int selectedRackSlot = -1;
    juce::String activePluginPath;
    juce::String startupWarning;
    bool preserveRecoveredState = false;
    juce::MemoryBlock pendingPluginState;
    bool bypassRequested = false;
    
    double currentSampleRate = 0.0;
    int currentNumChannels = 0;
    int currentBlockSize = 0;
    bool pluginPrepared = false;
    bool useDoublePrecision = false;
    int64_t processedSamples = 0;
    int lastLoggedLatency = -1;
    int processLogCountdown = 0;
    bool smoothNextProcessedBlock = false;
    bool hasOutputTail = false;
    juce::Array<double> lastOutputTail;
    std::atomic<int> lockMissCount { 0 };
    std::atomic<int> smoothedBypassCount { 0 };
    std::atomic<int> slowBlockCount { 0 };
    std::atomic<int> maxCallbackMicros { 0 };
    std::atomic<int> clippedOutputSamples { 0 };
    std::atomic<int> nonFiniteOutputSamples { 0 };
    std::atomic<int> maxOutputMilliAbs { 0 };
    std::atomic<juce::uint32> stateDirtyAt { 0 };
    std::atomic<juce::uint32> editorStateRevision { 0 };

    class SyntheticPlayHead : public juce::AudioPlayHead
    {
    public:
        juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override;
        void update(double sampleRate, int blockFrames, int64_t samplePosition);
        void reset();

    private:
        juce::AudioPlayHead::PositionInfo position;
    };
    
    juce::CriticalSection processLock;
    juce::AudioBuffer<float> floatBuffer;
    juce::AudioBuffer<double> doubleBuffer;
    juce::MidiBuffer midiBuffer;
    SyntheticPlayHead playHead;

    class ScanThread : public juce::Thread
    {
    public:
        ScanThread(VST3HostEngine& owner, juce::Array<juce::File> files, juce::Array<bridge::ScanFolder> folders,
                   bool force, juce::File loadAfterScan);
        void run() override;
    private:
        VST3HostEngine& owner;
        juce::Array<juce::File> files;
        juce::Array<bridge::ScanFolder> folders;
        bool force;
        juce::File loadAfterScan;
    };
    friend class ScanThread;
    void runScan(const juce::Array<juce::File>& files, bool force, const juce::File& loadAfterScan);
    bridge::PluginRecord scanOne(const juce::File& file);
    void rebuildPluginListFromCache();
    mutable juce::CriticalSection stateLock;
    std::unique_ptr<ScanThread> scanThread;
    std::atomic<int> scanDone { 0 };
    std::atomic<int> scanTotal { 0 };
    std::atomic<int> scanCompatible { 0 };
    std::atomic<int> scanFailed { 0 };
    std::atomic<int> scanTimedOut { 0 };
    juce::String scanCurrent;
    juce::String scanSummary;
    juce::StringArray scanIssues;
    std::function<void(bridge::Architecture)> hostArchitectureRequester;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VST3HostEngine)
};
