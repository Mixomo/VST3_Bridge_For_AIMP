#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "VST3HostEngine.h"
#include <juce_core/juce_core.h>
#include <cstdint>
#include <cstdio>
#include <vector>

// Raw pointer — no destructor fires at DLL_PROCESS_DETACH.
// Lifetime is controlled explicitly by createInstance() / destroyInstance()
// from the AIMP plugin's Initialize() and Finalize() callbacks.
static VST3HostEngine* s_instance = nullptr;

namespace
{
    std::vector<std::unique_ptr<juce::AudioPluginInstance>>& getRetiredPlugins()
    {
        // Intentionally leaked for process lifetime. Some commercial VST3s keep
        // internal GUI/timer/helper callbacks alive briefly after the host
        // releases the instance. Keeping retired instances and their modules
        // loaded avoids crashes in "*vst3_unloaded" code after hot-swaps.
        static auto* plugins = new std::vector<std::unique_ptr<juce::AudioPluginInstance>>();
        return *plugins;
    }

    bool isKnownUnsafePluginPath(const juce::String& path)
    {
        auto lower = path.toLowerCase();
        return lower.contains("fabfilter pro-q 4.vst3");
    }

    juce::String describeBuses(const juce::AudioProcessor& processor)
    {
        juce::String text;

        for (const auto isInput : { true, false })
        {
            text += isInput ? " inputs[" : " outputs[";
            for (int i = 0; i < processor.getBusCount(isInput); ++i)
            {
                if (auto* bus = processor.getBus(isInput, i))
                {
                    if (i > 0)
                        text += "; ";

                    text += "#" + juce::String(i)
                         + " \"" + bus->getName() + "\""
                         + " channels=" + juce::String(bus->getNumberOfChannels())
                         + " enabled=" + juce::String(bus->isEnabled() ? "yes" : "no")
                         + " layout=\"" + bus->getCurrentLayout().getDescription() + "\"";
                }
            }
            text += "]";
        }

        return text;
    }

    bool processBlockSafely(juce::AudioProcessor& processor,
                            juce::AudioBuffer<float>& buffer,
                            juce::MidiBuffer& midi)
    {
        __try
        {
            processor.processBlock(buffer, midi);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool processBlockSafely(juce::AudioProcessor& processor,
                            juce::AudioBuffer<double>& buffer,
                            juce::MidiBuffer& midi)
    {
        __try
        {
            processor.processBlock(buffer, midi);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    juce::File getLegacyConfigFile(void* dllInstance)
    {
        if (dllInstance != nullptr)
        {
            char dllPath[MAX_PATH] {};
            GetModuleFileNameA((HMODULE)dllInstance, dllPath, MAX_PATH);
            return juce::File(dllPath).getParentDirectory().getChildFile("dsp_vst3_config.json");
        }

        return juce::File::getSpecialLocation(juce::File::currentExecutableFile)
            .getParentDirectory().getChildFile("dsp_vst3_config.json");
    }

    double readInterleavedSample(const void* samples, const unsigned char* bytes, int bps, int sampleIndex)
    {
        if (bps == 8)
            return (static_cast<int>(bytes[sampleIndex]) - 128) / 128.0;

        if (bps == 16)
            return reinterpret_cast<const int16_t*>(samples)[sampleIndex] / 32768.0;

        if (bps == 24)
        {
            const auto* p = bytes + sampleIndex * 3;
            int32_t v = static_cast<int32_t>(p[0])
                      | (static_cast<int32_t>(p[1]) << 8)
                      | (static_cast<int32_t>(p[2]) << 16);
            if ((v & 0x00800000) != 0)
                v |= static_cast<int32_t>(0xff000000);
            return v / 8388608.0;
        }

        return reinterpret_cast<const int32_t*>(samples)[sampleIndex] / 2147483648.0;
    }

    void writeInterleavedSample(void* samples, unsigned char* bytes, int bps, int sampleIndex, double value)
    {
        const auto val = juce::jlimit(-1.0, 1.0, value);

        if (bps == 8)
            bytes[sampleIndex] = static_cast<unsigned char>(juce::jlimit(0, 255, juce::roundToInt(val * 127.0 + 128.0)));
        else if (bps == 16)
            reinterpret_cast<int16_t*>(samples)[sampleIndex] = static_cast<int16_t>(val * 32767.0);
        else if (bps == 24)
        {
            const auto v = juce::jlimit(-8388608, 8388607, juce::roundToInt(val * 8388607.0));
            auto* p = bytes + sampleIndex * 3;
            p[0] = static_cast<unsigned char>(v & 0xff);
            p[1] = static_cast<unsigned char>((v >> 8) & 0xff);
            p[2] = static_cast<unsigned char>((v >> 16) & 0xff);
        }
        else
            reinterpret_cast<int32_t*>(samples)[sampleIndex] = static_cast<int32_t>(juce::jlimit(-2147483647.0, 2147483647.0, val * 2147483647.0));
    }

    void updateAtomicMax(std::atomic<int>& target, int value)
    {
        auto previous = target.load(std::memory_order_relaxed);
        while (value > previous
               && !target.compare_exchange_weak(previous, value, std::memory_order_relaxed))
        {
        }
    }

    double sanitizeOutputSample(double value, int& clippedCount, int& nonFiniteCount, int& blockMaxMilliAbs)
    {
        if (!std::isfinite(value))
        {
            ++nonFiniteCount;
            return 0.0;
        }

        const auto absValue = std::abs(value);
        blockMaxMilliAbs = juce::jmax(blockMaxMilliAbs, juce::roundToInt(absValue * 1000.0));

        if (absValue > 1.0)
            ++clippedCount;

        return juce::jlimit(-1.0, 1.0, value);
    }
}

VST3HostEngine& VST3HostEngine::getInstance()
{
    jassert(s_instance != nullptr); // must call createInstance() first
    return *s_instance;
}

void VST3HostEngine::createInstance()
{
    if (s_instance != nullptr)
        return;

    s_instance = new VST3HostEngine();
}

void VST3HostEngine::destroyInstance()
{
    delete s_instance;   // calls ~VST3HostEngine() → shutdown() while JUCE is alive
    s_instance = nullptr;
}

VST3HostEngine::VST3HostEngine()
{
}

VST3HostEngine::~VST3HostEngine()
{
    shutdown();
}

void VST3HostEngine::init(void* hInst)
{
    dllInstance = hInst;
    writeLog("Host init");
    
    // Register the VST3 format
    if (formatManager.getNumFormats() == 0)
        formatManager.addFormat(new juce::VST3PluginFormat());
    
    // Restore cached plugin list and the last selected plugin. Scanning is
    // manual from the config window; doing it on DSP enable can load arbitrary
    // third-party modules while AIMP is toggling the effect.
    loadState();
}

void VST3HostEngine::shutdown()
{
    writeLog("Host shutdown");
    writeLog("Realtime stats: lockMisses=" + juce::String(lockMissCount.load())
             + " smoothedBypassBlocks=" + juce::String(smoothedBypassCount.load())
             + " slowBlocks=" + juce::String(slowBlockCount.load())
             + " maxCallbackMicros=" + juce::String(maxCallbackMicros.load())
             + " clippedOutputSamples=" + juce::String(clippedOutputSamples.load())
             + " nonFiniteOutputSamples=" + juce::String(nonFiniteOutputSamples.load())
             + " maxOutputAbs=" + juce::String(maxOutputMilliAbs.load() / 1000.0, 3));
    saveState();
    unloadPlugin();
}

juce::Array<juce::PluginDescription> VST3HostEngine::getAvailablePlugins() const
{
    return pluginList.getTypes();
}

bool VST3HostEngine::loadPlugin(const juce::String& path)
{
    // Ensure thread-safe plugin swapping
    const juce::ScopedLock sl(processLock);
    
    unloadPlugin();
    writeLog("Loading plugin: " + path);

    if (isPluginIncompatible(path))
    {
        writeLog("Plugin blocked and hidden from bridge list: " + path);
        quarantineFailedPlugin(path);
        return false;
    }
    
    juce::PluginDescription desc;
    desc.pluginFormatName = "VST3";
    desc.fileOrIdentifier = path;
    
    // Find matching description in our list if available to populate manufacturer, etc.
    for (const auto& d : pluginList.getTypes())
    {
        if (d.fileOrIdentifier == path)
        {
            desc = d;
            break;
        }
    }
    
    juce::String errorMsg;
    double sampleRate = currentSampleRate > 0.0 ? currentSampleRate : 44100.0;
    int blockSize = 512;
    
    auto instance = formatManager.createPluginInstance(desc, sampleRate, blockSize, errorMsg);
    if (instance != nullptr)
    {
        pluginInstance = std::move(instance);
        pluginInstance->setPlayHead(&playHead);
        pluginInstance->setNonRealtime(false);
        useDoublePrecision = pluginInstance->supportsDoublePrecisionProcessing();
        pluginInstance->setProcessingPrecision(useDoublePrecision ? juce::AudioProcessor::doublePrecision
                                                                  : juce::AudioProcessor::singlePrecision);
        pluginInstance->addListener(this);
        activePluginPath = path;
        rememberPluginDescription(desc);
        processLogCountdown = 8;
        lastLoggedLatency = -1;

        writeLog("Plugin loaded: name=\"" + pluginInstance->getName()
                 + "\" inputs=" + juce::String(pluginInstance->getTotalNumInputChannels())
                 + " outputs=" + juce::String(pluginInstance->getTotalNumOutputChannels())
                 + " inputBuses=" + juce::String(pluginInstance->getBusCount(true))
                 + " outputBuses=" + juce::String(pluginInstance->getBusCount(false))
                 + " doublePrecision=" + juce::String(useDoublePrecision ? "yes" : "no")
                 + " hasEditor=" + juce::String(pluginInstance->hasEditor() ? "yes" : "no"));
        writeLog("Initial bus state:" + describeBuses(*pluginInstance));
        
        if (currentSampleRate > 0.0 && currentNumChannels > 0)
        {
            preparePlugin(currentSampleRate, currentNumChannels, currentBlockSize > 0 ? currentBlockSize : 512);
        }
        
        return true;
    }
    
    writeLog("Plugin load failed: " + path + " error=\"" + errorMsg + "\"");
    quarantineFailedPlugin(path);
    return false;
}

bool VST3HostEngine::loadPluginFromFile(const juce::File& pluginFile)
{
    writeLog("Discovering plugin file: " + pluginFile.getFullPathName());

    if (!pluginFile.exists() || formatManager.getNumFormats() == 0)
    {
        writeLog("Discovery failed: file missing or format manager not ready");
        return false;
    }

    auto* format = formatManager.getFormat(0);
    juce::OwnedArray<juce::PluginDescription> descriptions;
    format->findAllTypesForFile(descriptions, pluginFile.getFullPathName());

    if (descriptions.isEmpty())
    {
        writeLog("Discovery failed: no VST3 descriptions found");
        return false;
    }

    auto desc = *descriptions[0];
    writeLog("Discovered plugin: name=\"" + desc.name
             + "\" manufacturer=\"" + desc.manufacturerName
             + "\" path=\"" + desc.fileOrIdentifier + "\"");
    rememberPluginDescription(desc);

    if (isPluginIncompatible(desc.fileOrIdentifier))
    {
        writeLog("Discovered plugin is blocked; hiding from bridge list: " + desc.fileOrIdentifier);
        quarantineFailedPlugin(desc.fileOrIdentifier);
        return false;
    }

    return loadPlugin(desc.fileOrIdentifier);
}

void VST3HostEngine::rememberPluginDescription(const juce::PluginDescription& desc)
{
    if (desc.fileOrIdentifier.isEmpty())
        return;

    for (const auto& existing : pluginList.getTypes())
    {
        if (existing.fileOrIdentifier == desc.fileOrIdentifier)
            return;
    }

    pluginList.addType(desc);
}

bool VST3HostEngine::isPluginIncompatible(const juce::String& path) const
{
    if (path.isEmpty())
        return false;

    return isKnownUnsafePluginPath(path);
}

bool VST3HostEngine::consumeBypassRequested()
{
    const juce::ScopedLock sl(processLock);
    const auto requested = bypassRequested;
    bypassRequested = false;
    return requested;
}

void VST3HostEngine::quarantineFailedPlugin(const juce::String& path)
{
    writeLog("Quarantining failed plugin and entering bridge bypass: " + path);
    removePluginFromList(path);
    bypassRequested = true;
}

void VST3HostEngine::removePluginFromList(const juce::String& path)
{
    if (path.isEmpty())
        return;

    if (activePluginPath.equalsIgnoreCase(path))
        unloadPlugin();

    juce::PluginDescription descToRemove;
    bool found = false;

    for (const auto& desc : pluginList.getTypes())
    {
        if (desc.fileOrIdentifier.equalsIgnoreCase(path))
        {
            descToRemove = desc;
            found = true;
            break;
        }
    }

    if (found)
        pluginList.removeType(descToRemove);

    writeLog("Removed plugin from dropdown: " + path);
    saveState();
}

void VST3HostEngine::unloadPlugin()
{
    const juce::ScopedLock sl(processLock);
    if (pluginInstance != nullptr)
    {
        writeLog("Retiring plugin without unloading module: " + activePluginPath);
        pluginInstance->suspendProcessing(true);
        const juce::ScopedLock callbackLock(pluginInstance->getCallbackLock());
        pluginInstance->setPlayHead(nullptr);
        pluginInstance->removeListener(this);
        pluginInstance->reset();
        pluginInstance->releaseResources();
        getRetiredPlugins().push_back(std::move(pluginInstance));
        writeLog("Plugin retired; retiredCount=" + juce::String((int)getRetiredPlugins().size()));
    }
    activePluginPath = "";
    pluginPrepared = false;
    useDoublePrecision = false;
    playHead.reset();
    processedSamples = 0;
}

int VST3HostEngine::processAudio(void* samples, int numFrames, int bps, int numChannels, int sampleRate)
{
    juce::ScopedNoDenormals noDenormals;

    if (samples == nullptr || numFrames <= 0 || numChannels <= 0 || sampleRate <= 0)
        return numFrames;

    if (bps != 8 && bps != 16 && bps != 24 && bps != 32)
        return numFrames;

    const auto callbackStartTicks = juce::Time::getHighResolutionTicks();
    auto* bytes = static_cast<unsigned char*>(samples);
    int blockClippedSamples = 0;
    int blockNonFiniteSamples = 0;
    int blockMaxMilliAbs = 0;

    auto updateOutputTail = [&]()
    {
        lastOutputTail.clearQuick();
        for (int c = 0; c < numChannels; ++c)
        {
            const auto sampleIndex = (numFrames - 1) * numChannels + c;
            lastOutputTail.add(readInterleavedSample(samples, bytes, bps, sampleIndex));
        }
        hasOutputTail = true;
    };

    auto smoothBlockStartFromTail = [&]()
    {
        if (!hasOutputTail || lastOutputTail.size() < numChannels)
            return;

        constexpr int smoothingSamples = 64;
        const int fadeSamples = juce::jmin(numFrames, smoothingSamples);
        if (fadeSamples <= 1)
            return;

        for (int i = 0; i < fadeSamples; ++i)
        {
            const double alpha = static_cast<double>(i + 1) / static_cast<double>(fadeSamples + 1);
            for (int c = 0; c < numChannels; ++c)
            {
                const auto sampleIndex = i * numChannels + c;
                const auto current = readInterleavedSample(samples, bytes, bps, sampleIndex);
                const auto smoothed = lastOutputTail[c] + (current - lastOutputTail[c]) * alpha;
                writeInterleavedSample(samples, bytes, bps, sampleIndex, smoothed);
            }
        }
    };

    const juce::ScopedTryLock sl(processLock);
    if (!sl.isLocked())
    {
        smoothBlockStartFromTail();
        updateOutputTail();
        smoothNextProcessedBlock = true;
        ++lockMissCount;
        ++smoothedBypassCount;
        return numFrames;
    }
    
    if (pluginInstance == nullptr)
        return numFrames;
    
    // Check if format changed and re-prepare if necessary
    if (sampleRate != currentSampleRate || numChannels != currentNumChannels || numFrames > currentBlockSize)
    {
        pluginPrepared = preparePlugin(sampleRate, numChannels, numFrames);
    }

    if (!pluginPrepared)
        return numFrames;

    playHead.update(sampleRate, numFrames, processedSamples);
    if (useDoublePrecision)
    {
        doubleBuffer.setSize(numChannels, numFrames, false, false, true);

        for (int c = 0; c < numChannels; ++c)
        {
            double* dest = doubleBuffer.getWritePointer(c);
            for (int i = 0; i < numFrames; ++i)
            {
                const auto sampleIndex = i * numChannels + c;

                if (bps == 8)
                    dest[i] = (static_cast<int>(bytes[sampleIndex]) - 128) / 128.0;
                else if (bps == 16)
                    dest[i] = reinterpret_cast<const int16_t*>(samples)[sampleIndex] / 32768.0;
                else if (bps == 24)
                {
                    const auto* p = bytes + sampleIndex * 3;
                    int32_t v = static_cast<int32_t>(p[0])
                              | (static_cast<int32_t>(p[1]) << 8)
                              | (static_cast<int32_t>(p[2]) << 16);
                    if ((v & 0x00800000) != 0)
                        v |= static_cast<int32_t>(0xff000000);
                    dest[i] = v / 8388608.0;
                }
                else
                    dest[i] = reinterpret_cast<const int32_t*>(samples)[sampleIndex] / 2147483648.0;
            }
        }

        midiBuffer.clear();
        {
            const juce::ScopedLock callbackLock(pluginInstance->getCallbackLock());
            if (pluginInstance->isSuspended())
                return numFrames;

            try
            {
                if (!processBlockSafely(*pluginInstance, doubleBuffer, midiBuffer))
                {
                    const auto failedPath = activePluginPath;
                    writeLog("Process block failed with Windows structured exception; bypassing bridge");
                    quarantineFailedPlugin(failedPath);
                    return numFrames;
                }
            }
            catch (...)
            {
                const auto failedPath = activePluginPath;
                writeLog("Process block failed with C++ exception; bypassing bridge");
                quarantineFailedPlugin(failedPath);
                return numFrames;
            }
        }

        for (int c = 0; c < numChannels; ++c)
        {
            const double* src = doubleBuffer.getReadPointer(c);
            for (int i = 0; i < numFrames; ++i)
            {
                auto val = sanitizeOutputSample(src[i], blockClippedSamples, blockNonFiniteSamples, blockMaxMilliAbs);
                const auto sampleIndex = i * numChannels + c;

                if (bps == 8)
                    bytes[sampleIndex] = static_cast<unsigned char>(juce::jlimit(0, 255, juce::roundToInt(val * 127.0 + 128.0)));
                else if (bps == 16)
                    reinterpret_cast<int16_t*>(samples)[sampleIndex] = static_cast<int16_t>(val * 32767.0);
                else if (bps == 24)
                {
                    const auto v = juce::jlimit(-8388608, 8388607, juce::roundToInt(val * 8388607.0));
                    auto* p = bytes + sampleIndex * 3;
                    p[0] = static_cast<unsigned char>(v & 0xff);
                    p[1] = static_cast<unsigned char>((v >> 8) & 0xff);
                    p[2] = static_cast<unsigned char>((v >> 16) & 0xff);
                }
                else
                    reinterpret_cast<int32_t*>(samples)[sampleIndex] = static_cast<int32_t>(juce::jlimit(-2147483647.0, 2147483647.0, val * 2147483647.0));
            }
        }
    }
    else
    {
        floatBuffer.setSize(numChannels, numFrames, false, false, true);

        for (int c = 0; c < numChannels; ++c)
        {
            float* dest = floatBuffer.getWritePointer(c);
            for (int i = 0; i < numFrames; ++i)
            {
                const auto sampleIndex = i * numChannels + c;

                if (bps == 8)
                    dest[i] = (static_cast<int>(bytes[sampleIndex]) - 128) / 128.0f;
                else if (bps == 16)
                    dest[i] = reinterpret_cast<const int16_t*>(samples)[sampleIndex] / 32768.0f;
                else if (bps == 24)
                {
                    const auto* p = bytes + sampleIndex * 3;
                    int32_t v = static_cast<int32_t>(p[0])
                              | (static_cast<int32_t>(p[1]) << 8)
                              | (static_cast<int32_t>(p[2]) << 16);
                    if ((v & 0x00800000) != 0)
                        v |= static_cast<int32_t>(0xff000000);
                    dest[i] = static_cast<float>(v / 8388608.0);
                }
                else
                    dest[i] = static_cast<float>(reinterpret_cast<const int32_t*>(samples)[sampleIndex] / 2147483648.0);
            }
        }

        midiBuffer.clear();
        {
            const juce::ScopedLock callbackLock(pluginInstance->getCallbackLock());
            if (pluginInstance->isSuspended())
                return numFrames;

            try
            {
                if (!processBlockSafely(*pluginInstance, floatBuffer, midiBuffer))
                {
                    const auto failedPath = activePluginPath;
                    writeLog("Process block failed with Windows structured exception; bypassing bridge");
                    quarantineFailedPlugin(failedPath);
                    return numFrames;
                }
            }
            catch (...)
            {
                const auto failedPath = activePluginPath;
                writeLog("Process block failed with C++ exception; bypassing bridge");
                quarantineFailedPlugin(failedPath);
                return numFrames;
            }
        }

        for (int c = 0; c < numChannels; ++c)
        {
            const float* src = floatBuffer.getReadPointer(c);
            for (int i = 0; i < numFrames; ++i)
            {
                auto val = static_cast<float>(sanitizeOutputSample(src[i], blockClippedSamples, blockNonFiniteSamples, blockMaxMilliAbs));
                const auto sampleIndex = i * numChannels + c;

                if (bps == 8)
                    bytes[sampleIndex] = static_cast<unsigned char>(juce::jlimit(0, 255, juce::roundToInt(val * 127.0f + 128.0f)));
                else if (bps == 16)
                    reinterpret_cast<int16_t*>(samples)[sampleIndex] = static_cast<int16_t>(val * 32767.0f);
                else if (bps == 24)
                {
                    const auto v = juce::jlimit(-8388608, 8388607, juce::roundToInt(val * 8388607.0f));
                    auto* p = bytes + sampleIndex * 3;
                    p[0] = static_cast<unsigned char>(v & 0xff);
                    p[1] = static_cast<unsigned char>((v >> 8) & 0xff);
                    p[2] = static_cast<unsigned char>((v >> 16) & 0xff);
                }
                else
                {
                    const auto scaled = static_cast<double>(val) * 2147483647.0;
                    reinterpret_cast<int32_t*>(samples)[sampleIndex] = static_cast<int32_t>(juce::jlimit(-2147483647.0, 2147483647.0, scaled));
                }
            }
        }
    }

    if (smoothNextProcessedBlock)
    {
        smoothBlockStartFromTail();
        smoothNextProcessedBlock = false;
    }

    updateOutputTail();
    processedSamples += numFrames;
    if (blockClippedSamples > 0)
        clippedOutputSamples.fetch_add(blockClippedSamples, std::memory_order_relaxed);
    if (blockNonFiniteSamples > 0)
        nonFiniteOutputSamples.fetch_add(blockNonFiniteSamples, std::memory_order_relaxed);
    updateAtomicMax(maxOutputMilliAbs, blockMaxMilliAbs);

    const auto elapsedTicks = juce::Time::getHighResolutionTicks() - callbackStartTicks;
    const auto elapsedMicros = static_cast<int>(elapsedTicks * 1000000 / juce::Time::getHighResolutionTicksPerSecond());
    updateAtomicMax(maxCallbackMicros, elapsedMicros);

    const auto blockMicros = static_cast<int>((static_cast<int64_t>(numFrames) * 1000000) / sampleRate);
    if (blockMicros > 0 && elapsedMicros > (blockMicros * 3) / 4)
        ++slowBlockCount;
    
    return numFrames;
}

bool VST3HostEngine::preparePlugin(double sampleRate, int numChannels, int blockSize)
{
    currentSampleRate = sampleRate;
    currentNumChannels = numChannels;
    currentBlockSize = juce::jmax(1, blockSize);
    pluginPrepared = false;
    lastOutputTail.ensureStorageAllocated(numChannels);
    
    if (pluginInstance == nullptr)
        return false;
        
    writeLog("Preparing plugin: sampleRate=" + juce::String(sampleRate)
             + " channels=" + juce::String(numChannels)
             + " blockSize=" + juce::String(currentBlockSize));

    pluginInstance->releaseResources();
    pluginInstance->setPlayHead(&playHead);
    pluginInstance->setNonRealtime(false);
    if (pluginInstance->supportsDoublePrecisionProcessing())
    {
        useDoublePrecision = true;
        pluginInstance->setProcessingPrecision(juce::AudioProcessor::doublePrecision);
    }
    else
    {
        useDoublePrecision = false;
        pluginInstance->setProcessingPrecision(juce::AudioProcessor::singlePrecision);
    }
    
    // AIMP's DSP pipeline is a single main bus. Commercial VST3s often expose
    // sidechain/aux buses by default; leave those disabled so the process
    // buffer contains only the main input/output channels.
    pluginInstance->disableNonMainBuses();

    juce::AudioProcessor::BusesLayout layout;
    const auto mainLayout = juce::AudioChannelSet::canonicalChannelSet(numChannels);

    for (int i = 0; i < pluginInstance->getBusCount(true); ++i)
        layout.inputBuses.add(i == 0 ? mainLayout : juce::AudioChannelSet::disabled());

    for (int i = 0; i < pluginInstance->getBusCount(false); ++i)
        layout.outputBuses.add(i == 0 ? mainLayout : juce::AudioChannelSet::disabled());
    
    if (!pluginInstance->setBusesLayout(layout))
    {
        writeLog("setBusesLayout with disabled non-main buses rejected; falling back to setPlayConfigDetails");
        pluginInstance->setPlayConfigDetails(numChannels, numChannels, sampleRate, currentBlockSize);
    }

    pluginInstance->setRateAndBufferSizeDetails(sampleRate, currentBlockSize);
    pluginInstance->prepareToPlay(sampleRate, currentBlockSize);
    pluginPrepared = true;
    processLogCountdown = 8;
    processedSamples = 0;

    writeLog("Prepare complete: inputs=" + juce::String(pluginInstance->getTotalNumInputChannels())
             + " outputs=" + juce::String(pluginInstance->getTotalNumOutputChannels())
             + " latency=" + juce::String(pluginInstance->getLatencySamples())
             + " tail=" + juce::String(pluginInstance->getTailLengthSeconds())
             + " precision=" + juce::String(useDoublePrecision ? "double" : "float"));
    writeLog("Prepared bus state:" + describeBuses(*pluginInstance));
    return true;
}

juce::File VST3HostEngine::getConfigFile() const
{
    auto configDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Mixomo")
        .getChildFile("VST3 Bridge");

    configDir.createDirectory();
    return configDir.getChildFile("dsp_vst3_bridge_config.json");
}

juce::File VST3HostEngine::getLogFile() const
{
    return juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("dsp_vst3_bridge.log");
}

void VST3HostEngine::writeLog(const juce::String& message) const
{
    auto logFile = getLogFile();
    auto timestamp = juce::Time::getCurrentTime().formatted("%Y-%m-%d %H:%M:%S.");
    auto millis = juce::String(juce::Time::getCurrentTime().getMilliseconds()).paddedLeft('0', 3);
    logFile.appendText(timestamp + millis + " " + message + "\r\n", false, false, "\r\n");
}

void VST3HostEngine::saveState()
{
    juce::File configFile = getConfigFile();
    
    juce::DynamicObject::Ptr rootObj = new juce::DynamicObject();
    
    // Save active plugin path
    rootObj->setProperty("activePluginPath", activePluginPath);
    
    // Save internal VST3 plugin state (as Base64)
    if (pluginInstance != nullptr)
    {
        const juce::ScopedLock callbackLock(pluginInstance->getCallbackLock());
        juce::MemoryBlock stateData;
        pluginInstance->getStateInformation(stateData);
        rootObj->setProperty("pluginState", stateData.toBase64Encoding());
    }
    
    // Save scanned plugin cache for faster loading
    juce::Array<juce::var> pluginArray;
    for (const auto& desc : pluginList.getTypes())
    {
        juce::DynamicObject::Ptr pObj = new juce::DynamicObject();
        pObj->setProperty("name", desc.name);
        pObj->setProperty("manufacturer", desc.manufacturerName);
        pObj->setProperty("path", desc.fileOrIdentifier);
        pObj->setProperty("uid", desc.uniqueId);
        pObj->setProperty("version", desc.version);
        pObj->setProperty("category", desc.category);
        pObj->setProperty("descriptiveName", desc.descriptiveName);
        pObj->setProperty("isInstrument", desc.isInstrument);
        pluginArray.add(juce::var(pObj.get()));
    }
    rootObj->setProperty("scannedPlugins", pluginArray);

    juce::var rootVar(rootObj.get());
    juce::String jsonStr = juce::JSON::toString(rootVar);
    configFile.replaceWithText(jsonStr);
}

void VST3HostEngine::loadState()
{
    juce::File configFile = getConfigFile();
    if (!configFile.existsAsFile())
    {
        auto legacyConfigFile = getLegacyConfigFile(dllInstance);
        if (!legacyConfigFile.existsAsFile())
            return;

        configFile = legacyConfigFile;
    }
        
    auto rootVar = juce::JSON::parse(configFile);
    auto* rootObj = rootVar.getDynamicObject();
    if (rootObj == nullptr)
        return;

    // Restore scanned plugin cache
    if (rootObj->hasProperty("scannedPlugins"))
    {
        auto* scannedArr = rootObj->getProperty("scannedPlugins").getArray();
        if (scannedArr != nullptr)
        {
            for (auto& pVar : *scannedArr)
            {
                auto* pObj = pVar.getDynamicObject();
                if (pObj != nullptr)
                {
                    juce::PluginDescription desc;
                    desc.pluginFormatName = "VST3";
                    desc.name = pObj->getProperty("name").toString();
                    desc.manufacturerName = pObj->getProperty("manufacturer").toString();
                    desc.fileOrIdentifier = pObj->getProperty("path").toString();
                    desc.uniqueId = static_cast<int>(pObj->getProperty("uid"));
                    desc.version = pObj->getProperty("version").toString();
                    desc.category = pObj->getProperty("category").toString();
                    desc.descriptiveName = pObj->getProperty("descriptiveName").toString();
                    desc.isInstrument = static_cast<bool>(pObj->getProperty("isInstrument"));
                    rememberPluginDescription(desc);
                }
            }
        }
    }
    
    // Restore active plugin and its state
    if (rootObj->hasProperty("activePluginPath"))
    {
        juce::String path = rootObj->getProperty("activePluginPath").toString();
        if (isPluginIncompatible(path))
        {
            writeLog("Skipping restore of unsafe plugin: " + path);
            quarantineFailedPlugin(path);
            return;
        }

        if (path.isNotEmpty() && loadPlugin(path))
        {
            if (rootObj->hasProperty("pluginState") && pluginInstance != nullptr)
            {
                juce::String stateBase64 = rootObj->getProperty("pluginState").toString();
                juce::MemoryBlock stateData;
                if (stateData.fromBase64Encoding(stateBase64))
                {
                    pluginInstance->suspendProcessing(true);
                    const juce::ScopedLock callbackLock(pluginInstance->getCallbackLock());
                    pluginInstance->setStateInformation(stateData.getData(), (int)stateData.getSize());
                    pluginInstance->suspendProcessing(false);
                }
            }
        }
    }
}

void VST3HostEngine::audioProcessorParameterChanged(juce::AudioProcessor*, int, float)
{
    // Never perform file I/O from a processor callback. State is persisted
    // when the selected plugin changes or the host shuts down.
}

void VST3HostEngine::audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&)
{
    // See audioProcessorParameterChanged().
}

juce::Optional<juce::AudioPlayHead::PositionInfo> VST3HostEngine::SyntheticPlayHead::getPosition() const
{
    return position;
}

void VST3HostEngine::SyntheticPlayHead::update(double sampleRate, int blockFrames, int64_t samplePosition)
{
    juce::ignoreUnused(blockFrames);

    const auto sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    const double seconds = static_cast<double>(samplePosition) / sr;
    constexpr double bpm = 120.0;
    const double ppq = seconds * bpm / 60.0;
    const auto bar = static_cast<int64_t>(ppq / 4.0);

    juce::AudioPlayHead::PositionInfo next;
    next.setTimeInSamples(samplePosition);
    next.setTimeInSeconds(seconds);
    next.setBpm(bpm);
    next.setTimeSignature(juce::AudioPlayHead::TimeSignature { 4, 4 });
    next.setPpqPosition(ppq);
    next.setPpqPositionOfLastBarStart(static_cast<double>(bar * 4));
    next.setBarCount(bar);
    next.setFrameRate(juce::AudioPlayHead::FrameRate(juce::AudioPlayHead::fps60));
    next.setEditOriginTime(0.0);
    next.setHostTimeNs(static_cast<uint64_t>(juce::Time::getHighResolutionTicks()
        * (1000000000.0 / juce::Time::getHighResolutionTicksPerSecond())));
    next.setIsPlaying(true);
    next.setIsRecording(false);
    next.setIsLooping(false);
    position = next;
}

void VST3HostEngine::SyntheticPlayHead::reset()
{
    position = {};
}
