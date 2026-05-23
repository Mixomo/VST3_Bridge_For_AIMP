#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_data_structures/juce_data_structures.h>
#include <atomic>

class VST3HostEngine : public juce::AudioProcessorListener
{
public:
    static VST3HostEngine& getInstance();

    void init(void* hInst);
    void shutdown();

    juce::Array<juce::PluginDescription> getAvailablePlugins() const;
    
    bool loadPlugin(const juce::String& path);
    bool loadPluginFromFile(const juce::File& pluginFile);
    void unloadPlugin();
    
    int processAudio(void* samples, int numFrames, int bps, int numChannels, int sampleRate);
    
    bool hasActivePlugin() const { return pluginInstance != nullptr; }
    juce::AudioPluginInstance* getPluginInstance() const { return pluginInstance.get(); }
    juce::String getActivePluginPath() const { return activePluginPath; }

    void saveState();
    void loadState();
    void rememberPluginDescription(const juce::PluginDescription& desc);
    bool isPluginIncompatible(const juce::String& path) const;
    bool consumeBypassRequested();
    void quarantineFailedPlugin(const juce::String& path);
    void removePluginFromList(const juce::String& path);

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
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList pluginList;
    
    std::unique_ptr<juce::AudioPluginInstance> pluginInstance;
    juce::String activePluginPath;
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VST3HostEngine)
};
