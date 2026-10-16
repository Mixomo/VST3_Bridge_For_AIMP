#include "OutProcAudioClient.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <new>
#include <vector>

namespace
{
    std::atomic<bool> watchAllocations { false };
    std::atomic<std::uint64_t> allocationCount { 0 };

    void require(bool condition, int line)
    {
        if (!condition)
        {
            std::cerr << "Requirement failed at line " << line << '\n';
            std::abort();
        }
    }

#define REQUIRE(condition) require((condition), __LINE__)

    struct SharedMemory
    {
        SharedMemory()
        {
            const auto size = vst3BridgeIpcMappingSize();
            data = ::operator new(size, std::align_val_t(alignof(VST3BridgeIpcHeader)));
            std::memset(data, 0, size);
            header = new (data) VST3BridgeIpcHeader();
        }

        ~SharedMemory()
        {
            header->~VST3BridgeIpcHeader();
            ::operator delete(data, std::align_val_t(alignof(VST3BridgeIpcHeader)));
        }

        void* data = nullptr;
        VST3BridgeIpcHeader* header = nullptr;
    };

    double inputSignal(std::uint64_t position, int channel)
    {
        return 0.25 * std::sin(static_cast<double>(position) * 0.003 + channel * 0.1);
    }

    void fillPcm(std::vector<std::int32_t>& samples, int frames, int channels,
                 std::uint64_t position)
    {
        for (int frame = 0; frame < frames; ++frame)
            for (int channel = 0; channel < channels; ++channel)
                samples[static_cast<std::size_t>(frame) * channels + channel] =
                    static_cast<std::int32_t>(inputSignal(position + frame, channel) * 2147483647.0);
    }

    void processPending(VST3BridgeIpcHeader& header, int latency,
                        std::array<std::vector<double>, VST3_BRIDGE_IPC_MAX_CHANNELS>& history,
                        std::uint64_t& hostPosition)
    {
        for (;;)
        {
            int selected = -1;
            auto sequence = UINT64_MAX;
            for (std::uint32_t i = 0; i < VST3_BRIDGE_IPC_SLOT_COUNT; ++i)
            {
                const auto& slot = header.slots[i];
                if (slot.state.load(std::memory_order_acquire) == VST3_BRIDGE_SLOT_REQUEST
                    && slot.sequence < sequence)
                {
                    selected = static_cast<int>(i);
                    sequence = slot.sequence;
                }
            }
            if (selected < 0)
                return;

            auto& slot = header.slots[static_cast<std::uint32_t>(selected)];
            auto expected = VST3_BRIDGE_SLOT_REQUEST;
            if (!slot.state.compare_exchange_strong(expected, VST3_BRIDGE_SLOT_PROCESSING,
                                                    std::memory_order_acquire))
                continue;

            auto* input = vst3BridgeIpcSlotInput(&header, static_cast<std::uint32_t>(selected));
            auto* output = vst3BridgeIpcSlotOutput(&header, static_cast<std::uint32_t>(selected));
            REQUIRE(slot.streamSample == hostPosition);
            for (std::uint32_t frame = 0; frame < slot.numFrames; ++frame, ++hostPosition)
            {
                for (std::uint32_t channel = 0; channel < slot.numChannels; ++channel)
                {
                    auto& channelHistory = history[channel];
                    const auto sample = input[static_cast<std::size_t>(channel) * slot.numFrames + frame];
                    REQUIRE(std::abs(sample - inputSignal(hostPosition, static_cast<int>(channel))) < 2.0e-9);
                    channelHistory[hostPosition % channelHistory.size()] = sample;
                    const auto delayed = hostPosition >= static_cast<std::uint64_t>(latency)
                        ? channelHistory[(hostPosition - latency) % channelHistory.size()] : 0.0;
                    output[static_cast<std::size_t>(channel) * slot.numFrames + frame] = delayed * 0.5;
                }
            }

            slot.status = VST3_BRIDGE_SLOT_OK;
            header.pluginLatency.store(static_cast<std::uint32_t>(latency), std::memory_order_release);
            header.pluginReady.store(1, std::memory_order_release);
            slot.state.store(VST3_BRIDGE_SLOT_RESPONSE, std::memory_order_release);
        }
    }

    void testTimelineAcrossFormatsAndCallbackSizes(int sampleRate, int channels, int pluginLatency)
    {
        SharedMemory memory;
        OutProcAudioClient client;
        REQUIRE(client.prepare());
        auto& header = *memory.header;

        constexpr std::array<int, 9> callbackSizes { 960, 1, 17, 32, 64, 128, 256, 480, 512 };
        constexpr int cycles = 600;
        constexpr int maxCallbackFrames = 960;
        const auto transportQuantum = (std::max)({
            static_cast<std::uint32_t>(VST3_BRIDGE_IPC_QUANTUM_FRAMES),
            static_cast<std::uint32_t>((sampleRate + 99) / 100),
            static_cast<std::uint32_t>(maxCallbackFrames) });
        const auto pipelineLatency = 2 * transportQuantum;
        std::vector<std::int32_t> pcm(static_cast<std::size_t>(maxCallbackFrames) * channels);
        std::array<std::vector<double>, VST3_BRIDGE_IPC_MAX_CHANNELS> history;
        for (int channel = 0; channel < channels; ++channel)
            history[static_cast<std::size_t>(channel)].assign(pluginLatency + 1, 0.0);

        std::uint64_t sequence = 0;
        std::uint64_t streamPosition = 0;
        std::uint64_t hostPosition = 0;
        std::uint32_t steadyUnderruns = 0;
        const auto initialAllocations = allocationCount.load(std::memory_order_relaxed);
        watchAllocations.store(true, std::memory_order_release);

        for (int cycle = 0; cycle < cycles; ++cycle)
        {
            const auto frames = callbackSizes[static_cast<std::size_t>(cycle) % callbackSizes.size()];
            fillPcm(pcm, frames, channels, streamPosition);
            client.process(pcm.data(), frames, 32, channels, sampleRate, &header, sequence);
            processPending(header, pluginLatency, history, hostPosition);

            for (int frame = 0; frame < frames; ++frame)
            {
                const auto current = streamPosition + static_cast<std::uint64_t>(frame);
                for (int channel = 0; channel < channels; ++channel)
                {
                    const auto sample = static_cast<double>(
                        pcm[static_cast<std::size_t>(frame) * channels + channel]) / 2147483648.0;
                    REQUIRE(std::isfinite(sample));
                    if (current > 10000 + pipelineLatency + pluginLatency)
                    {
                        const auto delayedPosition = current - pipelineLatency - pluginLatency;
                        const auto expected = inputSignal(delayedPosition, channel) * 0.5;
                        REQUIRE(std::abs(sample - expected) < 2.0e-8);
                    }
                }
            }

            streamPosition += static_cast<std::uint64_t>(frames);
            if (streamPosition > 10000)
                steadyUnderruns = header.underruns.load(std::memory_order_relaxed);
        }

        watchAllocations.store(false, std::memory_order_release);
        REQUIRE(allocationCount.load(std::memory_order_relaxed) == initialAllocations);
        REQUIRE(header.overruns.load(std::memory_order_relaxed) == 0);
        REQUIRE(header.underruns.load(std::memory_order_relaxed) == steadyUnderruns);
        REQUIRE(sequence > 10);
    }

    void testNonFiniteWetBlockIsRejected()
    {
        SharedMemory memory;
        OutProcAudioClient client;
        REQUIRE(client.prepare());
        auto& header = *memory.header;
        std::array<std::int32_t, 1024> pcm {};
        for (auto& sample : pcm)
            sample = 1000000;

        std::uint64_t sequence = 0;
        client.process(pcm.data(), 512, 32, 2, 48000, &header, sequence);
        auto& slot = header.slots[0];
        REQUIRE(slot.state.load(std::memory_order_acquire) == VST3_BRIDGE_SLOT_REQUEST);
        auto* output = vst3BridgeIpcSlotOutput(&header, 0);
        output[0] = std::numeric_limits<double>::quiet_NaN();
        slot.status = VST3_BRIDGE_SLOT_OK;
        slot.state.store(VST3_BRIDGE_SLOT_RESPONSE, std::memory_order_release);

        header.pluginReady.store(1, std::memory_order_release);
        client.process(pcm.data(), 512, 32, 2, 48000, &header, sequence);
        REQUIRE(header.staleBlocks.load(std::memory_order_relaxed) > 0);
        for (const auto sample : pcm)
            REQUIRE(std::isfinite(static_cast<double>(sample)));
    }

    void testSampleRateAndChannelChangesAdvanceEpoch()
    {
        SharedMemory memory;
        OutProcAudioClient client;
        REQUIRE(client.prepare());
        auto& header = *memory.header;
        std::uint64_t sequence = 0;
        std::array<std::int32_t, 1024> pcm {};

        client.process(pcm.data(), 512, 32, 2, 44100, &header, sequence);
        const auto initialEpoch = header.sessionEpoch.load(std::memory_order_acquire);
        client.process(pcm.data(), 512, 32, 2, 48000, &header, sequence);
        REQUIRE(header.sessionEpoch.load(std::memory_order_acquire) == initialEpoch + 1);
        client.process(pcm.data(), 512, 32, 1, 48000, &header, sequence);
        REQUIRE(header.sessionEpoch.load(std::memory_order_acquire) == initialEpoch + 2);
    }
}

void* operator new(std::size_t size)
{
    if (watchAllocations.load(std::memory_order_acquire))
        allocationCount.fetch_add(1, std::memory_order_relaxed);
    if (auto* memory = std::malloc(size))
        return memory;
    throw std::bad_alloc();
}

void* operator new[](std::size_t size)
{
    return ::operator new(size);
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

int main()
{
    testTimelineAcrossFormatsAndCallbackSizes(44100, 1, 0);
    testTimelineAcrossFormatsAndCallbackSizes(44100, 2, 257);
    testTimelineAcrossFormatsAndCallbackSizes(48000, 6, 257);
    testTimelineAcrossFormatsAndCallbackSizes(48000, 8, 0);
    testTimelineAcrossFormatsAndCallbackSizes(96000, 2, 257);
    testTimelineAcrossFormatsAndCallbackSizes(96000, 8, 0);
    testNonFiniteWetBlockIsRejected();
    testSampleRateAndChannelChangesAdvanceEpoch();
    std::cout << "OutProcAudioClientTests passed\n";
    return 0;
}
