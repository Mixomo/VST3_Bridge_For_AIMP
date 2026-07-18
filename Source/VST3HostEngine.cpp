#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "VST3HostEngine.h"
#include "OutProcProtocol.h"
#include <juce_core/juce_core.h>
#include <algorithm>
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
        juce::ignoreUnused(path);
        return false;
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

void VST3HostEngine::init(void* hInst, const bridge::RuntimePaths& paths, bool restorePlugin)
{
    dllInstance = hInst;
    runtimePaths = paths;
    configStore = std::make_unique<bridge::ConfigStore>(runtimePaths);
    writeLog("Host init; AIMP SDK " + juce::String(bridge::aimpSdkVersion)
             + "; architecture=" + bridge::architectureName(bridge::currentArchitecture())
             + "; packageRoot=" + runtimePaths.packageRoot.getFullPathName()
             + "; config=" + runtimePaths.activeConfig.getFullPathName());
    
    // Register the VST3 format
    if (formatManager.getNumFormats() == 0)
        formatManager.addFormat(std::make_unique<juce::VST3PluginFormat>());
    
    loadState(restorePlugin);
}

void VST3HostEngine::shutdown()
{
    cancelScan();
    if (scanThread != nullptr)
    {
        scanThread->stopThread(2000);
        scanThread.reset();
    }
    writeLog("Host shutdown");
    writeLog("Realtime stats: lockMisses=" + juce::String(lockMissCount.load())
             + " smoothedBypassBlocks=" + juce::String(smoothedBypassCount.load())
             + " slowBlocks=" + juce::String(slowBlockCount.load())
             + " maxCallbackMicros=" + juce::String(maxCallbackMicros.load())
             + " clippedOutputSamples=" + juce::String(clippedOutputSamples.load())
             + " nonFiniteOutputSamples=" + juce::String(nonFiniteOutputSamples.load())
             + " maxOutputAbs=" + juce::String(maxOutputMilliAbs.load() / 1000.0, 3));
    unloadPlugin();
}

juce::Array<juce::PluginDescription> VST3HostEngine::getAvailablePlugins() const
{
    const juce::ScopedLock lock(stateLock);
    return pluginList.getTypes();
}

bool VST3HostEngine::loadPlugin(const juce::String& path, bool restoreSavedState)
{
    const juce::ScopedLock sl(processLock);
    if (rackInstances.size() >= bridge::maxRackSlots)
    {
        writeLog("Rack is full; maximum is " + juce::String(bridge::maxRackSlots));
        return false;
    }
    pendingPluginState.reset();
    const auto bundle = bridge::vst3BundleRoot(juce::File(path));
    const auto loadPath = bundle.getFullPathName();
    writeLog("Loading plugin: " + loadPath);

    if (isPluginIncompatible(loadPath))
    {
        writeLog("Plugin blocked and hidden from bridge list: " + loadPath);
        return false;
    }

    auto* format = formatManager.getFormat(0);
    juce::OwnedArray<juce::PluginDescription> descriptions;
    if (format != nullptr) format->findAllTypesForFile(descriptions, loadPath);
    if (descriptions.isEmpty())
    {
        markPluginLoadFailure(loadPath, "No compatible VST3 class was found");
        return false;
    }
    auto desc = *descriptions[0];
    const auto canonical = bridge::canonicalPath(bundle);
    const auto records = getPluginRecords();
    for (const auto& record : records)
        if (record.canonicalPath.equalsIgnoreCase(canonical))
            for (const auto* candidate : descriptions)
                if (candidate->createIdentifierString() == record.uid) desc = *candidate;
    
    juce::String errorMsg;
    double sampleRate = currentSampleRate > 0.0 ? currentSampleRate : 44100.0;
    int blockSize = 512;
    
    auto instance = formatManager.createPluginInstance(desc, sampleRate, blockSize, errorMsg);
    if (instance != nullptr)
    {
        rackInstances.push_back({ loadPath, std::move(instance) });
        selectedRackSlot = static_cast<int>(rackInstances.size()) - 1;
        pluginInstance = rackInstances.back().plugin.get();
        pluginInstance->setPlayHead(&playHead);
        pluginInstance->setNonRealtime(false);
        useDoublePrecision = pluginInstance->supportsDoublePrecisionProcessing();
        pluginInstance->setProcessingPrecision(useDoublePrecision ? juce::AudioProcessor::doublePrecision
                                                                  : juce::AudioProcessor::singlePrecision);
        pluginInstance->addListener(this);
        activePluginPath = loadPath;
        rememberPluginDescription(desc);
        if (restoreSavedState)
        {
            const juce::ScopedLock lock(stateLock);
            for (const auto& record : settings.plugins)
                if (record.canonicalPath.equalsIgnoreCase(canonical) && record.state.isNotEmpty())
                {
                    pendingPluginState.fromBase64Encoding(record.state);
                    break;
                }
        }
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

        if (preserveRecoveredState)
        {
            const auto preserved = settings.activePlugin.resolve(runtimePaths);
            if (!preserved.getFullPathName().equalsIgnoreCase(loadPath)
                || !pendingPluginState.fromBase64Encoding(settings.pluginState))
                preserveRecoveredState = false;
        }
        
        if (currentSampleRate > 0.0 && currentNumChannels > 0)
        {
            preparePlugin(currentSampleRate, currentNumChannels, currentBlockSize > 0 ? currentBlockSize : 512);
        }
        
        return true;
    }
    
    writeLog("Plugin load failed: " + loadPath + " error=\"" + errorMsg + "\"");
    markPluginLoadFailure(loadPath, errorMsg);
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

    const juce::ScopedLock lock(stateLock);
    for (const auto& existing : pluginList.getTypes())
    {
        if (existing.fileOrIdentifier == desc.fileOrIdentifier)
            return;
    }

    pluginList.addType(desc);

    const auto bundle = bridge::vst3BundleRoot(juce::File(desc.fileOrIdentifier));
    const auto canonical = bridge::canonicalPath(bundle);
    for (const auto& record : settings.plugins)
        if (record.canonicalPath.equalsIgnoreCase(canonical)) return;
    bridge::PluginRecord record;
    record.location = bridge::PathReference::fromFile(bundle, runtimePaths);
    record.canonicalPath = canonical;
    record.name = desc.name;
    record.descriptiveName = desc.descriptiveName;
    record.manufacturer = desc.manufacturerName;
    record.version = desc.version;
    record.category = desc.category;
    record.uid = desc.createIdentifierString();
    record.architecture = bridge::detectBundleArchitecture(bundle);
    record.fingerprint = bridge::fingerprintBundle(bundle);
    record.instrument = desc.isInstrument;
    record.numInputChannels = desc.numInputChannels;
    record.numOutputChannels = desc.numOutputChannels;
    record.sharedContainer = desc.hasSharedContainer;
    record.araExtension = desc.hasARAExtension;
    record.lastScanTime = juce::Time::getCurrentTime().toMilliseconds();
    settings.plugins.add(record);
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
    writeLog("Quarantining failed rack slot: " + path);
    const auto canonical = bridge::canonicalPath(juce::File(path));
    const juce::ScopedLock lock(stateLock);
    for (auto& record : settings.plugins)
    {
        if (record.canonicalPath.equalsIgnoreCase(canonical))
        {
            record.runtimeQuarantined = true;
            record.status = "Runtime failed";
            record.lastError = "Plugin failed during runtime processing";
            break;
        }
    }
    for (const auto& slot : rackInstances)
        if (slot.path.equalsIgnoreCase(path)) slot.plugin->suspendProcessing(true);
}

void VST3HostEngine::markPluginLoadFailure(const juce::String& path, const juce::String& error)
{
    const auto canonical = bridge::canonicalPath(bridge::vst3BundleRoot(juce::File(path)));
    const juce::ScopedLock lock(stateLock);
    for (auto& record : settings.plugins)
        if (record.canonicalPath.equalsIgnoreCase(canonical))
        {
            record.status = "Load failed";
            record.lastError = error.isNotEmpty() ? error : "VST3 instance creation failed";
            record.runtimeQuarantined = false;
            break;
        }
}

void VST3HostEngine::removePluginFromList(const juce::String& path)
{
    if (path.isEmpty())
        return;

    for (int i = static_cast<int>(rackInstances.size()); --i >= 0;)
        if (rackInstances[static_cast<size_t>(i)].path.equalsIgnoreCase(path)) removeRackSlot(i);

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

    const auto canonical = bridge::canonicalPath(juce::File(path));
    {
        const juce::ScopedLock lock(stateLock);
        for (int i = settings.plugins.size(); --i >= 0;)
            if (settings.plugins.getReference(i).canonicalPath.equalsIgnoreCase(canonical)) settings.plugins.remove(i);
    }

    writeLog("Removed plugin from rack menu: " + path);
    saveState();
}

void VST3HostEngine::unloadPlugin()
{
    if (!rackInstances.empty()) saveState();
    const juce::ScopedLock sl(processLock);
    for (auto& slot : rackInstances)
    {
        writeLog("Retiring rack plugin without unloading module: " + slot.path);
        slot.plugin->suspendProcessing(true);
        const juce::ScopedLock callbackLock(slot.plugin->getCallbackLock());
        slot.plugin->setPlayHead(nullptr);
        slot.plugin->removeListener(this);
        slot.plugin->reset();
        slot.plugin->releaseResources();
        getRetiredPlugins().push_back(std::move(slot.plugin));
    }
    rackInstances.clear();
    pluginInstance = nullptr;
    selectedRackSlot = -1;
    activePluginPath = "";
    pluginPrepared = false;
    useDoublePrecision = false;
    playHead.reset();
    processedSamples = 0;
}

juce::StringArray VST3HostEngine::getRackPluginPaths() const
{
    juce::StringArray result;
    for (const auto& slot : rackInstances) result.add(slot.path);
    return result;
}

juce::AudioPluginInstance* VST3HostEngine::getRackPluginInstance(int index) const
{
    return juce::isPositiveAndBelow(index, static_cast<int>(rackInstances.size()))
        ? rackInstances[static_cast<size_t>(index)].plugin.get() : nullptr;
}

bool VST3HostEngine::isRackSlotMuted(int index) const
{
    return juce::isPositiveAndBelow(index, static_cast<int>(rackInstances.size()))
        && rackInstances[static_cast<size_t>(index)].muted;
}

bool VST3HostEngine::isRackSlotSolo(int index) const
{
    return juce::isPositiveAndBelow(index, static_cast<int>(rackInstances.size()))
        && rackInstances[static_cast<size_t>(index)].solo;
}

int VST3HostEngine::getRackSlotEditorHeight(int index) const
{
    return juce::isPositiveAndBelow(index, static_cast<int>(rackInstances.size()))
        ? rackInstances[static_cast<size_t>(index)].editorHeight : 0;
}

void VST3HostEngine::setRackSlotMuted(int index, bool muted)
{
    if (!juce::isPositiveAndBelow(index, static_cast<int>(rackInstances.size()))) return;
    const juce::ScopedLock sl(processLock);
    rackInstances[static_cast<size_t>(index)].muted = muted;
    saveState();
}

void VST3HostEngine::setRackSlotSolo(int index, bool solo)
{
    if (!juce::isPositiveAndBelow(index, static_cast<int>(rackInstances.size()))) return;
    const juce::ScopedLock sl(processLock);
    rackInstances[static_cast<size_t>(index)].solo = solo;
    saveState();
}

void VST3HostEngine::setRackSlotEditorHeight(int index, int height)
{
    if (!juce::isPositiveAndBelow(index, static_cast<int>(rackInstances.size()))) return;
    rackInstances[static_cast<size_t>(index)].editorHeight = juce::jlimit(120, 2400, height);
    saveState();
}

void VST3HostEngine::selectRackSlot(int index)
{
    if (!juce::isPositiveAndBelow(index, static_cast<int>(rackInstances.size()))) return;
    selectedRackSlot = index;
    pluginInstance = rackInstances[static_cast<size_t>(index)].plugin.get();
    activePluginPath = rackInstances[static_cast<size_t>(index)].path;
    ++editorStateRevision;
}

void VST3HostEngine::moveRackSlot(int from, int to)
{
    const auto size = static_cast<int>(rackInstances.size());
    if (!juce::isPositiveAndBelow(from, size) || !juce::isPositiveAndBelow(to, size) || from == to) return;
    const juce::ScopedLock sl(processLock);
    auto slot = std::move(rackInstances[static_cast<size_t>(from)]);
    rackInstances.erase(rackInstances.begin() + from);
    rackInstances.insert(rackInstances.begin() + to, std::move(slot));
    selectedRackSlot = to;
    pluginInstance = rackInstances[static_cast<size_t>(to)].plugin.get();
    activePluginPath = rackInstances[static_cast<size_t>(to)].path;
    ++editorStateRevision;
    saveState();
}

void VST3HostEngine::removeRackSlot(int index)
{
    if (!juce::isPositiveAndBelow(index, static_cast<int>(rackInstances.size()))) return;
    saveState();
    const juce::ScopedLock sl(processLock);
    auto& slot = rackInstances[static_cast<size_t>(index)];
    slot.plugin->suspendProcessing(true);
    slot.plugin->setPlayHead(nullptr);
    slot.plugin->removeListener(this);
    slot.plugin->releaseResources();
    getRetiredPlugins().push_back(std::move(slot.plugin));
    rackInstances.erase(rackInstances.begin() + index);
    pluginPrepared = false;
    if (rackInstances.empty())
    {
        selectedRackSlot = -1; pluginInstance = nullptr; activePluginPath.clear();
    }
    else selectRackSlot(juce::jmin(index, static_cast<int>(rackInstances.size()) - 1));
    saveState();
}

void VST3HostEngine::clearRack()
{
    unloadPlugin();
    saveState();
}

bool VST3HostEngine::cloneRackSlot(int index, int insertAt)
{
    if (!juce::isPositiveAndBelow(index, static_cast<int>(rackInstances.size()))
        || rackInstances.size() >= bridge::maxRackSlots) return false;
    juce::MemoryBlock state;
    const auto path = rackInstances[static_cast<size_t>(index)].path;
    const auto muted = rackInstances[static_cast<size_t>(index)].muted;
    const auto solo = rackInstances[static_cast<size_t>(index)].solo;
    const auto editorHeight = rackInstances[static_cast<size_t>(index)].editorHeight;
    {
        const juce::ScopedLock callbackLock(rackInstances[static_cast<size_t>(index)].plugin->getCallbackLock());
        rackInstances[static_cast<size_t>(index)].plugin->getStateInformation(state);
    }
    if (!loadPlugin(path, false)) return false;
    pluginInstance->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    rackInstances.back().muted = muted;
    rackInstances.back().solo = solo;
    rackInstances.back().editorHeight = editorHeight;
    moveRackSlot(static_cast<int>(rackInstances.size()) - 1,
                 juce::jlimit(0, static_cast<int>(rackInstances.size()) - 1, insertAt));
    saveState();
    return true;
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
    
    if (rackInstances.empty())
        return numFrames;
    
    // Check if format changed and re-prepare if necessary
    if (bridge::rackNeedsPreparation(pluginPrepared, currentSampleRate, currentNumChannels, currentBlockSize,
                                     sampleRate, numChannels, numFrames))
    {
        pluginPrepared = preparePlugin(sampleRate, numChannels, numFrames);
    }

    if (!pluginPrepared)
        return numFrames;

    playHead.update(sampleRate, numFrames, processedSamples);
    floatBuffer.setSize(numChannels, numFrames, false, false, true);

    for (int c = 0; c < numChannels; ++c)
    {
        float* dest = floatBuffer.getWritePointer(c);
        for (int i = 0; i < numFrames; ++i)
        {
            const auto sampleIndex = i * numChannels + c;
            dest[i] = static_cast<float>(readInterleavedSample(samples, bytes, bps, sampleIndex));
        }
    }

    // ponytail: one float chain keeps mixed plug-in capabilities simple; add per-slot
    // precision conversion only if a measured plug-in requires double processing.
    const auto anySolo = std::any_of(rackInstances.begin(), rackInstances.end(), [](const auto& slot) { return slot.solo; });
    for (const auto& slot : rackInstances)
    {
        if (slot.muted || (anySolo && !slot.solo)) continue;
        midiBuffer.clear();
        auto& processor = *slot.plugin;
        const juce::ScopedLock callbackLock(processor.getCallbackLock());
        if (processor.isSuspended()) continue;
        try
        {
            if (!processBlockSafely(processor, floatBuffer, midiBuffer))
            {
                writeLog("Rack process block failed with Windows structured exception: " + slot.path);
                quarantineFailedPlugin(slot.path);
                return numFrames;
            }
        }
        catch (...)
        {
            writeLog("Rack process block failed with C++ exception: " + slot.path);
            quarantineFailedPlugin(slot.path);
            return numFrames;
        }
    }

    for (int c = 0; c < numChannels; ++c)
        for (int i = 0; i < numFrames; ++i)
        {
            const auto value = sanitizeOutputSample(floatBuffer.getSample(c, i), blockClippedSamples,
                                                    blockNonFiniteSamples, blockMaxMilliAbs);
            writeInterleavedSample(samples, bytes, bps, i * numChannels + c, value);
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
    
    if (rackInstances.empty())
        return false;

    writeLog("Preparing rack: sampleRate=" + juce::String(sampleRate)
             + " channels=" + juce::String(numChannels)
             + " blockSize=" + juce::String(currentBlockSize));
    for (const auto& slot : rackInstances)
    {
        auto& processor = *slot.plugin;
        processor.releaseResources();
        processor.setPlayHead(&playHead);
        processor.setNonRealtime(false);
        processor.setProcessingPrecision(juce::AudioProcessor::singlePrecision);
        processor.disableNonMainBuses();

        juce::AudioProcessor::BusesLayout layout;
        const auto mainLayout = juce::AudioChannelSet::canonicalChannelSet(numChannels);
        for (int i = 0; i < processor.getBusCount(true); ++i)
            layout.inputBuses.add(i == 0 ? mainLayout : juce::AudioChannelSet::disabled());
        for (int i = 0; i < processor.getBusCount(false); ++i)
            layout.outputBuses.add(i == 0 ? mainLayout : juce::AudioChannelSet::disabled());
        if (!processor.setBusesLayout(layout))
            processor.setPlayConfigDetails(numChannels, numChannels, sampleRate, currentBlockSize);
        processor.setRateAndBufferSizeDetails(sampleRate, currentBlockSize);
        processor.prepareToPlay(sampleRate, currentBlockSize);
        writeLog("Prepared rack slot: " + slot.path + "; latency=" + juce::String(processor.getLatencySamples()));
    }
    pluginPrepared = true;
    useDoublePrecision = false;
    processLogCountdown = 8;
    processedSamples = 0;

    writeLog("Rack prepare complete; slots=" + juce::String(static_cast<int>(rackInstances.size())));
    return true;
}

juce::File VST3HostEngine::getConfigFile() const
{
    return runtimePaths.activeConfig;
}

juce::File VST3HostEngine::getLogFile() const
{
    return runtimePaths.logFile;
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
    if (configStore == nullptr)
        return;

    const juce::ScopedLock stateGuard(stateLock);
    if (preserveRecoveredState)
    {
        juce::String error;
        if (!configStore->save(settings, &error))
            writeLog("Configuration save failed: " + error);
        else
            writeLog("Safe startup configuration saved without replacing the preserved plugin state");
        return;
    }
    settings.rack.clear();
    std::size_t savedStateBytes = 0;
    for (const auto& slot : rackInstances)
    {
        stateDirtyAt = 0;
        const juce::ScopedLock callbackLock(slot.plugin->getCallbackLock());
        juce::MemoryBlock stateData;
        slot.plugin->getStateInformation(stateData);
        savedStateBytes += stateData.getSize();
        const auto encoded = stateData.toBase64Encoding();
        settings.rack.add({ bridge::PathReference::fromFile(juce::File(slot.path), runtimePaths), encoded,
                            slot.muted, slot.solo, slot.editorHeight });
        const auto canonical = bridge::canonicalPath(juce::File(slot.path));
        for (auto& record : settings.plugins)
            if (record.canonicalPath.equalsIgnoreCase(canonical)) { record.state = encoded; break; }
    }
    settings.activePlugin = activePluginPath.isEmpty() ? bridge::PathReference{}
        : bridge::PathReference::fromFile(juce::File(activePluginPath), runtimePaths);
    settings.pluginState = juce::isPositiveAndBelow(selectedRackSlot, settings.rack.size())
        ? settings.rack[selectedRackSlot].state : juce::String();
    juce::String error;
    if (!configStore->save(settings, &error))
        writeLog("Configuration save failed: " + error);
    else
        writeLog("Rack configuration saved; slots=" + juce::String(settings.rack.size())
                 + "; stateBytes=" + juce::String(static_cast<juce::int64>(savedStateBytes)));
}

void VST3HostEngine::flushPendingStateSave()
{
    auto changedAt = stateDirtyAt.load();
    if (changedAt == 0 || juce::Time::getMillisecondCounter() - changedAt < 1000) return;
    if (stateDirtyAt.compare_exchange_strong(changedAt, 0)) saveState();
}

void VST3HostEngine::loadState(bool restorePlugin)
{
    if (configStore == nullptr) return;
    settings = configStore->load();
    rebuildPluginListFromCache();

    if (!restorePlugin)
    {
        preserveRecoveredState = true;
        startupWarning = "The previous VST3 session failed its startup check. The bridge opened in bypass; your saved selection and state were preserved.";
        writeLog("Safe startup enabled; previous plugin state preserved and bridge opened in bypass");
        return;
    }

    if (settings.startupMode == "none") return;
    const auto savedRack = settings.rack;
    writeLog("Restoring rack; slots=" + juce::String(savedRack.size()));
    for (const auto& savedSlot : savedRack)
    {
        const auto path = savedSlot.plugin.resolve(runtimePaths);
        if (!path.exists()) continue;
        const auto canonical = bridge::canonicalPath(path);
        bool compatible = true;
        for (const auto& record : settings.plugins)
            if (record.canonicalPath.equalsIgnoreCase(canonical))
                compatible = !record.runtimeQuarantined && !record.scannerQuarantined && record.status == "Compatible";
        if (!compatible || !loadPlugin(path.getFullPathName(), false)) continue;
        rackInstances.back().muted = savedSlot.muted;
        rackInstances.back().solo = savedSlot.solo;
        rackInstances.back().editorHeight = savedSlot.editorHeight;
        pendingPluginState.fromBase64Encoding(savedSlot.state);
        applyPendingPluginState();
    }
}

void VST3HostEngine::applyPendingPluginState()
{
    if (pluginInstance == nullptr || pendingPluginState.isEmpty()) return;
    pluginInstance->suspendProcessing(true);
    {
        const juce::ScopedLock callbackLock(pluginInstance->getCallbackLock());
        pluginInstance->setStateInformation(pendingPluginState.getData(), static_cast<int>(pendingPluginState.getSize()));
    }
    pluginInstance->suspendProcessing(false);
    writeLog("Plugin state restored; bytes="
             + juce::String(static_cast<juce::int64>(pendingPluginState.getSize())));
    pendingPluginState.reset();
    preserveRecoveredState = false;
}

VST3HostEngine::ScanThread::ScanThread(VST3HostEngine& ownerIn, juce::Array<juce::File> filesIn,
                                       juce::Array<bridge::ScanFolder> foldersIn, bool forceIn, juce::File loadAfterScanIn)
    : juce::Thread("VST3 rack scanner"), owner(ownerIn), files(std::move(filesIn)),
      folders(std::move(foldersIn)), force(forceIn), loadAfterScan(std::move(loadAfterScanIn))
{
}

void VST3HostEngine::ScanThread::run()
{
    if (!folders.isEmpty()) files = bridge::discoverVst3Candidates(folders, owner.runtimePaths);
    owner.runScan(files, force, loadAfterScan);
}

void VST3HostEngine::scanAll(bool force)
{
    if (isScanning()) return;
    const auto snapshot = getSettingsSnapshot();
    auto folders = snapshot.scanFolders;
    if (!snapshot.scanBridgeFolder)
    {
        for (int i = folders.size(); --i >= 0;)
        {
            const auto resolved = folders.getReference(i).location.resolve(runtimePaths);
            if (resolved == runtimePaths.packageRoot || resolved == runtimePaths.packageRoot.getChildFile("VST3"))
                folders.remove(i);
        }
    }
    if (snapshot.scanSystemFolders)
    {
        const auto programFiles = juce::File::getSpecialLocation(juce::File::globalApplicationsDirectory);
        const auto programFilesX86 = juce::File(juce::SystemStats::getEnvironmentVariable("ProgramFiles(x86)", {}));
        folders.add({ bridge::PathReference::fromFile(programFiles.getChildFile("Common Files").getChildFile("VST3"), runtimePaths), true, true });
        if (programFilesX86 != juce::File())
            folders.add({ bridge::PathReference::fromFile(programFilesX86.getChildFile("Common Files").getChildFile("VST3"), runtimePaths), true, true });
    }
    scanThread = std::make_unique<ScanThread>(*this, juce::Array<juce::File>(), std::move(folders), force, juce::File());
    scanThread->startThread();
}

void VST3HostEngine::scanFolder(const bridge::ScanFolder& folder, bool force)
{
    if (isScanning()) return;
    juce::Array<bridge::ScanFolder> folders { folder };
    scanThread = std::make_unique<ScanThread>(*this, juce::Array<juce::File>(), std::move(folders), force, juce::File());
    scanThread->startThread();
}

void VST3HostEngine::scanFileAndLoad(const juce::File& file)
{
    if (isScanning()) return;
    juce::Array<juce::File> files { file };
    scanThread = std::make_unique<ScanThread>(*this, std::move(files), juce::Array<bridge::ScanFolder>(), true, file);
    scanThread->startThread();
}

void VST3HostEngine::cancelScan()
{
    if (scanThread != nullptr) scanThread->signalThreadShouldExit();
}

bool VST3HostEngine::isScanning() const
{
    return scanThread != nullptr && scanThread->isThreadRunning();
}

juce::String VST3HostEngine::getScanProgress() const
{
    const juce::ScopedLock lock(stateLock);
    if (!isScanning()) return scanSummary;
    return "Scanning " + juce::String(scanDone.load() + 1) + " of " + juce::String(scanTotal.load())
        + " - " + scanCurrent + " - compatible " + juce::String(scanCompatible.load())
        + ", failed " + juce::String(scanFailed.load()) + ", timed out " + juce::String(scanTimedOut.load());
}

bridge::BridgeSettings VST3HostEngine::getSettingsSnapshot() const
{
    const juce::ScopedLock lock(stateLock);
    return settings;
}

bridge::RuntimePaths VST3HostEngine::getRuntimePaths() const
{
    return runtimePaths;
}

void VST3HostEngine::updateSettings(const bridge::BridgeSettings& newSettings)
{
    const bool storageChanged = newSettings.requestedStorageMode != settings.requestedStorageMode;
    {
        const juce::ScopedLock lock(stateLock);
        settings = newSettings;
    }
    if (storageChanged && configStore != nullptr)
    {
        juce::String error;
        if (configStore->changeStorageMode(settings.requestedStorageMode, settings, &error))
            runtimePaths = configStore->getPaths();
        else
            writeLog("Storage mode change failed: " + error);
    }
    else
        saveState();
}

juce::Array<bridge::PluginRecord> VST3HostEngine::getPluginRecords() const
{
    const juce::ScopedLock lock(stateLock);
    return settings.plugins;
}

void VST3HostEngine::retryPlugin(const juce::String& path)
{
    const juce::File file(path);
    {
        const juce::ScopedLock lock(stateLock);
        for (auto& record : settings.plugins)
            if (record.canonicalPath.equalsIgnoreCase(bridge::canonicalPath(file)))
            {
                record.scannerQuarantined = false;
                record.runtimeQuarantined = false;
                record.lastError.clear();
            }
    }
    scanFileAndLoad(file);
}

void VST3HostEngine::resetScanCache()
{
    {
        const juce::ScopedLock lock(stateLock);
        for (auto& plugin : settings.plugins)
        {
            plugin.fingerprint.clear();
            plugin.lastError.clear();
            plugin.scannerQuarantined = false;
            plugin.runtimeQuarantined = false;
            plugin.status = "Compatible";
        }
    }
    rebuildPluginListFromCache();
    saveState();
}

void VST3HostEngine::resetActivePluginParameters()
{
    const auto index = selectedRackSlot;
    const auto path = activePluginPath;
    if (path.isEmpty() || index < 0) return;
    removeRackSlot(index);
    if (!loadPlugin(path, false)) return;
    moveRackSlot(static_cast<int>(rackInstances.size()) - 1, juce::jmin(index, static_cast<int>(rackInstances.size()) - 1));
    saveState();
    writeLog("Active plugin recreated with factory parameters: " + activePluginPath);
}

void VST3HostEngine::forgetAllPlugins()
{
    unloadPlugin();
    {
        const juce::ScopedLock lock(stateLock);
        settings.plugins.clear();
        settings.activePlugin = {};
        settings.pluginState.clear();
        pluginList.clear();
    }
    saveState();
}

juce::String VST3HostEngine::getDiagnostics() const
{
    const auto snapshot = getSettingsSnapshot();
    int scannerQuarantine = 0, runtimeQuarantine = 0;
    for (const auto& plugin : snapshot.plugins)
    {
        scannerQuarantine += plugin.scannerQuarantined ? 1 : 0;
        runtimeQuarantine += plugin.runtimeQuarantined ? 1 : 0;
    }
    juce::String text;
    text << "VST3 Bridge Rack For AIMP\r\n"
         << "Bridge version: " << bridge::bridgeVersion << " (config schema " << bridge::configSchemaVersion << ")\r\n"
         << "AIMP SDK: " << bridge::aimpSdkVersion << "\r\n"
         << "IPC version: " << VST3_BRIDGE_IPC_VERSION << "\r\n"
         << "Helper architecture: " << bridge::architectureName(bridge::currentArchitecture()) << "\r\n"
         << "Process DPI awareness: " << (AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(), DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) ? "PerMonitorV2" : "other") << "\r\n"
         << "System DPI: " << GetDpiForSystem() << " (" << juce::roundToInt(GetDpiForSystem() * 100.0 / 96.0) << "%)\r\n"
         << "Active plugin: " << (activePluginPath.isNotEmpty() ? activePluginPath : "[Bypass]") << "\r\n"
         << "Rack slots: " << static_cast<int>(rackInstances.size()) << "/" << bridge::maxRackSlots << "\r\n"
         << "Configuration: " << runtimePaths.activeConfig.getFullPathName() << "\r\n"
         << "Bridge root: " << runtimePaths.packageRoot.getFullPathName() << "\r\n"
         << "Log: " << runtimePaths.logFile.getFullPathName() << "\r\n"
         << "Cached plugins: " << snapshot.plugins.size() << "\r\n"
         << "Scanner quarantine: " << scannerQuarantine << "; runtime quarantine: " << runtimeQuarantine << "\r\n";
    for (const auto& folder : snapshot.scanFolders)
        text << "Scan folder: " << folder.location.resolve(runtimePaths).getFullPathName() << "\r\n";
    return text;
}

void VST3HostEngine::setHostArchitectureRequester(std::function<void(bridge::Architecture)> callback)
{
    hostArchitectureRequester = std::move(callback);
}

void VST3HostEngine::requestHostArchitecture(const bridge::PluginRecord& plugin)
{
    unloadPlugin();
    {
        const juce::ScopedLock lock(stateLock);
        settings.activePlugin = plugin.location;
        settings.pluginState = plugin.state;
        settings.rack.clear();
        settings.rack.add({ plugin.location, plugin.state });
    }
    juce::String error;
    if (configStore != nullptr && !configStore->save(settings, &error))
        writeLog("Architecture switch configuration save failed: " + error);
    if (hostArchitectureRequester) hostArchitectureRequester(plugin.architecture);
}

void VST3HostEngine::runScan(const juce::Array<juce::File>& files, bool force, const juce::File& loadAfterScan)
{
    scanDone = 0; scanTotal = files.size(); scanCompatible = 0; scanFailed = 0; scanTimedOut = 0;
    {
        const juce::ScopedLock lock(stateLock);
        scanSummary.clear();
        scanIssues.clear();
    }
    bool loadSucceeded = false;
    for (const auto& file : files)
    {
        if (scanThread->threadShouldExit()) break;
        const auto bundle = bridge::vst3BundleRoot(file);
        const auto canonical = bridge::canonicalPath(bundle);
        const auto fingerprint = bridge::fingerprintBundle(bundle);
        bool cached = false;
        {
            const juce::ScopedLock lock(stateLock);
            scanCurrent = file.getFileName();
            for (const auto& record : settings.plugins)
                if (record.canonicalPath.equalsIgnoreCase(canonical) && record.fingerprint == fingerprint
                    && record.status == "Compatible" && !force)
                {
                    cached = true;
                    ++scanCompatible;
                    break;
                }
        }

        bool compatible = cached;
        if (!cached)
        {
            auto result = scanOne(bundle);
            {
                const juce::ScopedLock lock(stateLock);
                int index = -1;
                for (int i = 0; i < settings.plugins.size(); ++i)
                    if (settings.plugins.getReference(i).canonicalPath.equalsIgnoreCase(canonical)) { index = i; break; }
                if (index >= 0)
                {
                    result.state = settings.plugins.getReference(index).state;
                    settings.plugins.set(index, result);
                }
                else settings.plugins.add(result);
                compatible = result.status == "Compatible";
                if (compatible) ++scanCompatible;
                else if (result.status == "Timed out") ++scanTimedOut;
                else ++scanFailed;
                if (!compatible)
                    scanIssues.add(file.getFileName() + ": " + result.status
                        + (result.lastError.isNotEmpty() ? " (" + result.lastError + ")" : juce::String()));
            }
            if (!compatible)
                writeLog("Scanner skipped " + file.getFullPathName() + ": " + result.status + " - " + result.lastError);
        }
        ++scanDone;
        loadSucceeded |= compatible && loadAfterScan != juce::File()
            && canonical.equalsIgnoreCase(bridge::canonicalPath(bridge::vst3BundleRoot(loadAfterScan)));
    }

    {
        const juce::ScopedLock lock(stateLock);
        const bool cancelled = scanThread->threadShouldExit();
        scanSummary = (cancelled ? "Scan cancelled: " : "Scan complete: ")
            + juce::String(scanDone.load()) + " of " + juce::String(scanTotal.load())
            + ", compatible " + juce::String(scanCompatible.load())
            + ", failed " + juce::String(scanFailed.load())
            + ", timed out " + juce::String(scanTimedOut.load());
        if (!scanIssues.isEmpty()) scanSummary += " | skipped: " + scanIssues.joinIntoString("; ");
    }
    writeLog(scanSummary);

    rebuildPluginListFromCache();
    saveState();
    if (loadSucceeded && !scanThread->threadShouldExit())
    {
        const auto path = loadAfterScan.getFullPathName();
        juce::MessageManager::callAsync([path]
        {
            if (s_instance != nullptr && s_instance->loadPlugin(path)) s_instance->saveState();
        });
    }
}

bridge::PluginRecord VST3HostEngine::scanOne(const juce::File& file)
{
    const auto bundle = bridge::vst3BundleRoot(file);
    bridge::PluginRecord record;
    record.location = bridge::PathReference::fromFile(bundle, runtimePaths);
    record.canonicalPath = bridge::canonicalPath(bundle);
    record.architecture = bridge::detectBundleArchitecture(bundle);
    record.fingerprint = bridge::fingerprintBundle(bundle);
    record.lastScanTime = juce::Time::getCurrentTime().toMilliseconds();

    auto scannerArchitecture = record.architecture;
    if (scannerArchitecture == bridge::Architecture::unknown || scannerArchitecture == bridge::Architecture::multi)
        scannerArchitecture = bridge::currentArchitecture();
    const auto scanner = runtimePaths.scanner(scannerArchitecture);
    if (!scanner.existsAsFile())
    {
        record.status = "Wrong architecture";
        record.lastError = "Required scanner is missing: " + scanner.getFullPathName();
        return record;
    }

    const auto resultFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("vst3_scan_" + juce::Uuid().toString(), ".json", false);
    juce::StringArray arguments { scanner.getFullPathName(), "--plugin", bundle.getFullPathName(),
                                  "--result", resultFile.getFullPathName() };
    juce::ChildProcess child;
    if (!child.start(arguments, 0))
    {
        record.status = "Scan failed";
        record.lastError = "Scanner process could not start";
        return record;
    }

    const auto timeoutSeconds = getSettingsSnapshot().scanTimeoutSeconds;
    const auto timeoutMs = juce::jlimit(1000, 120000, timeoutSeconds * 1000);
    int elapsed = 0;
    while (child.isRunning() && elapsed < timeoutMs && !scanThread->threadShouldExit())
    {
        child.waitForProcessToFinish(100);
        elapsed += 100;
    }
    if (child.isRunning())
    {
        child.kill();
        child.waitForProcessToFinish(2000);
        record.status = scanThread->threadShouldExit() ? "Disabled" : "Timed out";
        record.lastError = scanThread->threadShouldExit() ? "Scan cancelled"
            : "Scanner exceeded " + juce::String(timeoutSeconds) + " seconds and was skipped";
        record.scannerQuarantined = !scanThread->threadShouldExit();
        resultFile.deleteFile();
        return record;
    }

    const auto value = juce::JSON::parse(resultFile);
    resultFile.deleteFile();
    auto* object = value.getDynamicObject();
    if (object == nullptr)
    {
        record.status = "Scan failed";
        record.lastError = "Malformed scanner result";
        record.scannerQuarantined = true;
        return record;
    }
    const auto success = static_cast<bool>(object->getProperty("success"));
    record.name = object->getProperty("name").toString();
    record.descriptiveName = object->getProperty("descriptiveName").toString();
    record.manufacturer = object->getProperty("manufacturer").toString();
    record.version = object->getProperty("version").toString();
    record.category = object->getProperty("category").toString();
    record.uid = object->getProperty("uid").toString();
    record.instrument = static_cast<bool>(object->getProperty("instrument"));
    record.numInputChannels = static_cast<int>(object->getProperty("numInputChannels"));
    record.numOutputChannels = static_cast<int>(object->getProperty("numOutputChannels"));
    record.sharedContainer = static_cast<bool>(object->getProperty("sharedContainer"));
    record.araExtension = static_cast<bool>(object->getProperty("araExtension"));
    record.architecture = bridge::parseArchitecture(object->getProperty("architecture").toString());
    record.lastError = object->getProperty("message").toString();
    const auto scannerStatus = object->getProperty("status").toString();
    record.status = success ? "Compatible" : (scannerStatus == "wrong architecture" ? "Wrong architecture" : "Scan failed");
    record.scannerQuarantined = !success;
    return record;
}

void VST3HostEngine::rebuildPluginListFromCache()
{
    const juce::ScopedLock lock(stateLock);
    pluginList.clear();
    for (auto& record : settings.plugins)
    {
        const auto file = record.location.resolve(runtimePaths);
        if (!file.exists())
        {
            record.status = "Missing";
            if (settings.removeMissing) continue;
        }
        if (record.status != "Compatible" || record.scannerQuarantined || record.runtimeQuarantined) continue;
        juce::PluginDescription description;
        description.pluginFormatName = "VST3";
        description.name = record.name;
        description.descriptiveName = record.descriptiveName.isNotEmpty() ? record.descriptiveName : record.name;
        description.manufacturerName = record.manufacturer;
        description.version = record.version;
        description.category = record.category;
        description.fileOrIdentifier = file.getFullPathName();
        description.uniqueId = record.uid.hashCode();
        description.isInstrument = record.instrument;
        description.numInputChannels = record.numInputChannels;
        description.numOutputChannels = record.numOutputChannels;
        description.hasSharedContainer = record.sharedContainer;
        description.hasARAExtension = record.araExtension;
        pluginList.addType(description);
    }
}

void VST3HostEngine::audioProcessorParameterChanged(juce::AudioProcessor*, int, float)
{
    stateDirtyAt = juce::Time::getMillisecondCounter();
}

void VST3HostEngine::audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& details)
{
    stateDirtyAt = juce::Time::getMillisecondCounter();
    if (details.programChanged || details.nonParameterStateChanged)
        ++editorStateRevision;
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
