#pragma once

#include "OutProcProtocol.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

class OutProcAudioClient
{
public:
    bool prepare()
    {
        constexpr auto maxRate = 192000u;
        constexpr auto maxPipeline = 2 * maxSupportedCallbackFrames;
        capacity = maxRate * maxPluginDelaySeconds + maxPipeline
                 + VST3_BRIDGE_IPC_QUANTUM_FRAMES + 1;
        const auto storageSize = static_cast<std::size_t>(VST3_BRIDGE_IPC_MAX_CHANNELS) * capacity;
        dry.assign(storageSize, 0.0);
        wet.assign(storageSize, 0.0);
        accumulated.assign(vst3BridgeIpcSlotSamples(), 0.0);
        stamps.assign(capacity, invalidStamp);
        epochs.assign(capacity, 0);
        last.fill(0.0);
        currentSampleRate = 0;
        currentChannels = 0;
        currentEpoch = 0;
        maxCallbackFrames = 0;
        pipelineLatency = 0;
        previousPipelineLatency = 0;
        pluginDelay = 0;
        previousPluginDelay = 0;
        position = 0;
        accumulatedFrames = 0;
        blend = 0.0;
        delayBlend = 1.0;
        prepared = true;
        return true;
    }

    bool process(void* samples, int numFrames, int bitsPerSample, int numChannels,
                 int sampleRate, VST3BridgeIpcHeader* shared, std::uint64_t& sequence) noexcept
    {
        if (!prepared || samples == nullptr || shared == nullptr || numFrames <= 0
            || numFrames > 65536 || numChannels <= 0
            || numChannels > static_cast<int>(VST3_BRIDGE_IPC_MAX_CHANNELS)
            || sampleRate <= 0 || sampleRate > maxSampleRate
            || (bitsPerSample != 8 && bitsPerSample != 16
                && bitsPerSample != 24 && bitsPerSample != 32))
            return false;

        auto epoch = shared->sessionEpoch.load(std::memory_order_acquire);
        const bool formatChanged = sampleRate != currentSampleRate || numChannels != currentChannels;
        const bool callbackSizeGrew = static_cast<std::uint32_t>(numFrames) > maxCallbackFrames;
        if (formatChanged || callbackSizeGrew)
        {
            epoch = shared->sessionEpoch.fetch_add(1, std::memory_order_acq_rel) + 1;
            if (epoch == 0)
                epoch = shared->sessionEpoch.fetch_add(1, std::memory_order_acq_rel) + 1;
            maxCallbackFrames = static_cast<std::uint32_t>(numFrames);
            const auto transportQuantum = (std::max)({
                static_cast<std::uint32_t>(VST3_BRIDGE_IPC_QUANTUM_FRAMES),
                static_cast<std::uint32_t>((sampleRate + 99) / 100),
                maxCallbackFrames });
            resetFormat(sampleRate, numChannels, epoch, 2 * transportQuantum);
            shared->pluginReady.store(0, std::memory_order_release);
            shared->pluginLatency.store(0, std::memory_order_release);
        }
        else if (epoch != currentEpoch)
        {
            resetFormat(sampleRate, numChannels, epoch, pipelineLatency);
            shared->pluginReady.store(0, std::memory_order_release);
            shared->pluginLatency.store(0, std::memory_order_release);
        }

        const auto nextDelay = shared->pluginLatency.load(std::memory_order_acquire);
        delaySupported = nextDelay <= static_cast<std::uint32_t>(sampleRate * maxPluginDelaySeconds)
                      && nextDelay + pipelineLatency + VST3_BRIDGE_IPC_QUANTUM_FRAMES < capacity;
        if (delaySupported && nextDelay != pluginDelay)
        {
            previousPluginDelay = pluginDelay;
            previousPipelineLatency = pipelineLatency;
            pluginDelay = nextDelay;
            delayBlend = 0.0;
        }

        const bool formatValid = shared->magic == VST3_BRIDGE_IPC_MAGIC
                              && shared->version == VST3_BRIDGE_IPC_VERSION
                              && shared->quantumFrames == VST3_BRIDGE_IPC_QUANTUM_FRAMES
                              && shared->maxChannels == VST3_BRIDGE_IPC_MAX_CHANNELS;
        const bool hostReady = formatValid
            && shared->pluginReady.load(std::memory_order_acquire) != 0
            && shared->sessionEpoch.load(std::memory_order_acquire) == currentEpoch
            && delaySupported;

        collectResponses(shared);

        auto* pcm = static_cast<unsigned char*>(samples);
        bool missed = false;
        bool submitted = false;
        const auto fadeStep = 1.0 / (std::max)(1, sampleRate / 100);

        for (int frame = 0; frame < numFrames; ++frame, ++position)
        {
            const auto ringIndex = static_cast<std::size_t>(position % capacity);
            const bool ready = hostReady
                && stamps[ringIndex] == position
                && epochs[ringIndex] == currentEpoch
                && delayBlend >= 1.0;
            const auto targetBlend = ready ? 1.0 : 0.0;
            blend += (std::clamp)(targetBlend - blend, -fadeStep, fadeStep);
            delayBlend = (std::min)(1.0, delayBlend + fadeStep);
            missed |= !ready && position >= pipelineLatency + pluginDelay;

            for (int channel = 0; channel < numChannels; ++channel)
            {
                const auto sampleIndex = static_cast<std::size_t>(frame) * numChannels + channel;
                const auto inputSample = readPcmSample(pcm, bitsPerSample, sampleIndex);
                const auto source = std::isfinite(inputSample) ? inputSample : 0.0;
                const auto channelBase = static_cast<std::size_t>(channel) * capacity;
                dry[channelBase + ringIndex] = source;
                accumulated[static_cast<std::size_t>(channel) * VST3_BRIDGE_IPC_QUANTUM_FRAMES
                            + accumulatedFrames] = source;

                const auto currentLatency = pipelineLatency + pluginDelay;
                const auto oldLatency = previousPipelineLatency + previousPluginDelay;
                const auto newDry = position >= currentLatency
                    ? dry[channelBase + (position - currentLatency) % capacity] : 0.0;
                const auto oldDry = position >= oldLatency
                    ? dry[channelBase + (position - oldLatency) % capacity] : 0.0;
                const auto delayed = oldDry + (newDry - oldDry) * delayBlend;
                const auto wetSample = ready ? wet[channelBase + ringIndex]
                                             : last[static_cast<std::size_t>(channel)];
                const auto outputSample = delayed + (wetSample - delayed) * blend;
                writePcmSample(pcm, bitsPerSample, sampleIndex,
                               std::isfinite(outputSample) ? outputSample : delayed);
                if (ready)
                    last[static_cast<std::size_t>(channel)] = wetSample;
            }

            if (++accumulatedFrames == VST3_BRIDGE_IPC_QUANTUM_FRAMES)
            {
                submitted |= submitInput(shared, numChannels, sampleRate, epoch, sequence);
                accumulatedFrames = 0;
            }
        }

        if (missed)
            shared->underruns.fetch_add(1, std::memory_order_relaxed);
        return submitted;
    }

private:
    static constexpr std::uint64_t invalidStamp = ~std::uint64_t(0);
    static constexpr int maxSampleRate = 192000;
    static constexpr std::uint32_t maxPluginDelaySeconds = 2;
    static constexpr std::uint32_t maxSupportedCallbackFrames = 65536;

    static double readPcmSample(const unsigned char* bytes, int bitsPerSample, std::size_t index)
    {
        if (bitsPerSample == 8)
            return (static_cast<int>(bytes[index]) - 128) / 128.0;
        if (bitsPerSample == 16)
        {
            std::int16_t value = 0;
            std::memcpy(&value, bytes + index * 2, sizeof(value));
            return value / 32768.0;
        }
        if (bitsPerSample == 24)
        {
            const auto* sample = bytes + index * 3;
            std::int32_t value = static_cast<std::int32_t>(sample[0])
                               | (static_cast<std::int32_t>(sample[1]) << 8)
                               | (static_cast<std::int32_t>(sample[2]) << 16);
            if ((value & 0x00800000) != 0)
                value |= static_cast<std::int32_t>(0xff000000);
            return value / 8388608.0;
        }

        std::int32_t value = 0;
        std::memcpy(&value, bytes + index * 4, sizeof(value));
        return value / 2147483648.0;
    }

    static void writePcmSample(unsigned char* bytes, int bitsPerSample,
                               std::size_t index, double sample)
    {
        sample = (std::clamp)(sample, -1.0, 1.0);
        if (bitsPerSample == 8)
        {
            bytes[index] = static_cast<unsigned char>((std::clamp)(
                static_cast<int>(std::lround(sample * 127.0 + 128.0)), 0, 255));
        }
        else if (bitsPerSample == 16)
        {
            const auto value = static_cast<std::int16_t>(sample * 32767.0);
            std::memcpy(bytes + index * 2, &value, sizeof(value));
        }
        else if (bitsPerSample == 24)
        {
            const auto value = static_cast<std::int32_t>((std::clamp)(
                static_cast<long>(std::lround(sample * 8388607.0)), -8388608L, 8388607L));
            auto* destination = bytes + index * 3;
            destination[0] = static_cast<unsigned char>(value & 0xff);
            destination[1] = static_cast<unsigned char>((value >> 8) & 0xff);
            destination[2] = static_cast<unsigned char>((value >> 16) & 0xff);
        }
        else
        {
            const auto value = static_cast<std::int32_t>(sample * 2147483647.0);
            std::memcpy(bytes + index * 4, &value, sizeof(value));
        }
    }

    void resetFormat(int sampleRate, int channels, std::uint64_t epoch,
                     std::uint32_t nextPipelineLatency)
    {
        currentSampleRate = sampleRate;
        currentChannels = channels;
        currentEpoch = epoch;
        pipelineLatency = nextPipelineLatency;
        previousPipelineLatency = nextPipelineLatency;
        position = 0;
        accumulatedFrames = 0;
        pluginDelay = 0;
        previousPluginDelay = 0;
        delayBlend = 1.0;
        blend = 0.0;
    }

    void collectResponses(VST3BridgeIpcHeader* shared)
    {
        for (auto& slot : shared->slots)
        {
            auto expected = VST3_BRIDGE_SLOT_RESPONSE;
            if (!slot.state.compare_exchange_strong(expected, VST3_BRIDGE_SLOT_READING,
                                                    std::memory_order_acquire))
                continue;

            bool valid = slot.status == VST3_BRIDGE_SLOT_OK
                && slot.numFrames == VST3_BRIDGE_IPC_QUANTUM_FRAMES
                && slot.numChannels == static_cast<std::uint32_t>(currentChannels)
                && slot.sampleRate == static_cast<std::uint32_t>(currentSampleRate)
                && slot.epoch == currentEpoch;
            const auto slotIndex = static_cast<std::uint32_t>(&slot - shared->slots);
            const auto* source = vst3BridgeIpcSlotOutput(shared, slotIndex);
            const auto sampleCount = static_cast<std::size_t>(slot.numChannels) * slot.numFrames;
            for (std::size_t sample = 0; valid && sample < sampleCount; ++sample)
                valid = std::isfinite(source[sample]);
            const auto targetStart = slot.streamSample + pipelineLatency;
            if (valid && targetStart >= position && targetStart < position + capacity)
            {
                for (std::uint32_t frame = 0; frame < VST3_BRIDGE_IPC_QUANTUM_FRAMES; ++frame)
                {
                    const auto target = targetStart + frame;
                    const auto index = static_cast<std::size_t>(target % capacity);
                    for (int channel = 0; channel < currentChannels; ++channel)
                        wet[static_cast<std::size_t>(channel) * capacity + index] =
                            source[static_cast<std::size_t>(channel)
                                   * VST3_BRIDGE_IPC_QUANTUM_FRAMES + frame];
                    stamps[index] = target;
                    epochs[index] = slot.epoch;
                }
            }
            else
            {
                shared->staleBlocks.fetch_add(1, std::memory_order_relaxed);
            }

            slot.state.store(VST3_BRIDGE_SLOT_EMPTY, std::memory_order_release);
        }
    }

    bool submitInput(VST3BridgeIpcHeader* shared, int channels, int sampleRate,
                     std::uint64_t epoch, std::uint64_t& sequence)
    {
        for (auto& slot : shared->slots)
        {
            auto expected = VST3_BRIDGE_SLOT_EMPTY;
            if (!slot.state.compare_exchange_strong(expected, VST3_BRIDGE_SLOT_WRITING,
                                                    std::memory_order_acquire))
                continue;

            const auto slotIndex = static_cast<std::uint32_t>(&slot - shared->slots);
            auto* destination = vst3BridgeIpcSlotInput(shared, slotIndex);
            for (int channel = 0; channel < channels; ++channel)
                std::memcpy(destination + static_cast<std::size_t>(channel)
                                    * VST3_BRIDGE_IPC_QUANTUM_FRAMES,
                            accumulated.data() + static_cast<std::size_t>(channel)
                                    * VST3_BRIDGE_IPC_QUANTUM_FRAMES,
                            VST3_BRIDGE_IPC_QUANTUM_FRAMES * sizeof(double));
            slot.status = VST3_BRIDGE_SLOT_BYPASS;
            slot.numFrames = VST3_BRIDGE_IPC_QUANTUM_FRAMES;
            slot.numChannels = static_cast<std::uint32_t>(channels);
            slot.sampleRate = static_cast<std::uint32_t>(sampleRate);
            slot.epoch = epoch;
            slot.sequence = ++sequence;
            slot.streamSample = position + 1 - VST3_BRIDGE_IPC_QUANTUM_FRAMES;
            slot.state.store(VST3_BRIDGE_SLOT_REQUEST, std::memory_order_release);
            return true;
        }

        shared->overruns.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    bool prepared = false;
    std::uint32_t capacity = 0;
    int currentSampleRate = 0;
    int currentChannels = 0;
    std::uint64_t currentEpoch = 0;
    std::uint32_t maxCallbackFrames = 0;
    std::uint32_t pipelineLatency = 0;
    std::uint32_t previousPipelineLatency = 0;
    std::uint32_t pluginDelay = 0;
    std::uint32_t previousPluginDelay = 0;
    std::uint64_t position = 0;
    std::uint32_t accumulatedFrames = 0;
    double blend = 0.0;
    double delayBlend = 1.0;
    bool delaySupported = true;
    std::array<double, VST3_BRIDGE_IPC_MAX_CHANNELS> last {};
    std::vector<double> dry;
    std::vector<double> wet;
    std::vector<double> accumulated;
    std::vector<std::uint64_t> stamps;
    std::vector<std::uint64_t> epochs;
};
