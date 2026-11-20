#include "OutProcAudioClient.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <new>
#include <thread>
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

    struct Events
    {
        Events()
        {
            request = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            response = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            shutdown = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        }

        ~Events()
        {
            CloseHandle(request);
            CloseHandle(response);
            CloseHandle(shutdown);
        }

        HANDLE request = nullptr;
        HANDLE response = nullptr;
        HANDLE shutdown = nullptr;
    };

    constexpr double hostGain = 0.5;

    void spinFor(int micros)
    {
        if (micros <= 0)
            return;
        const auto until = std::chrono::steady_clock::now() + std::chrono::microseconds(micros);
        while (std::chrono::steady_clock::now() < until) {}
    }

    // Answers pending requests in sequence order with gain-scaled input,
    // mirroring OutProcHostMain's audio thread. `delayMicros` is the cost of
    // each block. When `checkContiguous` is set, blocks must cover the stream
    // without gaps.
    void answerPending(VST3BridgeIpcHeader& header, HANDLE response, int delayMicros,
                       bool checkContiguous, std::uint64_t& expectedStream)
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

            REQUIRE(slot.numFrames > 0 && slot.numFrames <= VST3_BRIDGE_IPC_MAX_BLOCK_FRAMES);
            if (checkContiguous)
            {
                REQUIRE(expectedStream == 0 || slot.streamSample == expectedStream);
                expectedStream = slot.streamSample + slot.numFrames;
            }
            spinFor(delayMicros);
            const auto* input = vst3BridgeIpcSlotInput(&header, static_cast<std::uint32_t>(selected));
            auto* output = vst3BridgeIpcSlotOutput(&header, static_cast<std::uint32_t>(selected));
            const auto count = static_cast<std::size_t>(slot.numFrames) * slot.numChannels;
            for (std::size_t i = 0; i < count; ++i)
                output[i] = input[i] * hostGain;
            slot.status = slot.epoch == header.sessionEpoch.load() ? VST3_BRIDGE_SLOT_OK : VST3_BRIDGE_SLOT_BYPASS;
            slot.state.store(VST3_BRIDGE_SLOT_RESPONSE, std::memory_order_release);
            SetEvent(response);
        }
    }

    struct FakeHost
    {
        FakeHost(VST3BridgeIpcHeader& h, Events& e, int delayMicros = 0, bool checkContiguous = true)
            : header(h), events(e)
        {
            header.hostAudioReady.store(1, std::memory_order_release);
            thread = std::thread([this, delayMicros, checkContiguous]
            {
                HANDLE waits[] = { events.shutdown, events.request };
                while (WaitForMultipleObjects(2, waits, FALSE, INFINITE) == WAIT_OBJECT_0 + 1)
                    answerPending(header, events.response, delayMicros, checkContiguous, streamPosition);
            });
        }

        ~FakeHost()
        {
            SetEvent(events.shutdown);
            thread.join();
            ResetEvent(events.shutdown);
        }

        VST3BridgeIpcHeader& header;
        Events& events;
        std::uint64_t streamPosition = 0;
        std::thread thread;
    };

    double inputSignal(std::uint64_t position, int channel)
    {
        return 0.25 * std::sin(static_cast<double>(position) * 0.003 + channel * 0.1);
    }

    void fillPcm(std::vector<std::int32_t>& samples, int frames, int channels, std::uint64_t position)
    {
        for (int frame = 0; frame < frames; ++frame)
            for (int channel = 0; channel < channels; ++channel)
                samples[static_cast<std::size_t>(frame) * channels + channel] =
                    static_cast<std::int32_t>(inputSignal(position + frame, channel) * 2147483647.0);
    }

    double pcmAt(const std::vector<std::int32_t>& pcm, int frame, int channels, int channel)
    {
        return pcm[static_cast<std::size_t>(frame) * channels + channel] / 2147483648.0;
    }

    // Expected output at stream frame `index` for a constant pipeline latency.
    double delayed(std::uint64_t index, std::uint32_t latency, int channel, double gain)
    {
        return index >= latency ? inputSignal(index - latency, channel) * gain : 0.0;
    }

    std::int64_t qpcFrequency()
    {
        LARGE_INTEGER value {};
        QueryPerformanceFrequency(&value);
        return value.QuadPart;
    }

    // AIMP with DirectSound/WASAPI fills its buffer in bursts: callbacks arrive
    // back to back with varying sizes. Every frame after the pipeline latency
    // must be host output.
    void testBurstyCallbacksAreFullyProcessed(int sampleRate, int channels, int hostDelayMicros)
    {
        SharedMemory memory;
        Events events;
        OutProcAudioClient client;
        REQUIRE(client.prepare());
        auto& header = *memory.header;
        FakeHost host(header, events, hostDelayMicros);

        constexpr std::array<int, 11> callbackSizes { 4410, 1, 17, 480, 512, 1024, 4096, 4097, 9000, 2205, 20000 };
        std::vector<std::int32_t> pcm(static_cast<std::size_t>(20000) * channels);
        std::uint64_t sequence = 0;
        std::uint64_t streamPosition = 0;

        const auto initialAllocations = allocationCount.load(std::memory_order_relaxed);
        watchAllocations.store(true, std::memory_order_release);
        std::uint32_t latency = 0;
        for (int cycle = 0; cycle < 300; ++cycle)
        {
            const auto frames = callbackSizes[static_cast<std::size_t>(cycle) % callbackSizes.size()];
            fillPcm(pcm, frames, channels, streamPosition);
            client.process(pcm.data(), frames, 32, channels, sampleRate, &header, sequence,
                           events.request, events.response);
            latency = client.getLatencyFrames();
            for (int frame = 0; frame < frames; ++frame)
                for (int channel = 0; channel < channels; ++channel)
                {
                    const auto got = pcmAt(pcm, frame, channels, channel);
                    const auto want = delayed(streamPosition + frame, latency, channel, hostGain);
                    if (std::abs(got - want) >= 1.0e-6)
                    {
                        const auto& st = client.getStats();
                        std::cerr << "cycle=" << cycle << " frames=" << frames << " frame=" << frame
                                  << " pos=" << streamPosition + frame << " got=" << got << " want=" << want
                                  << " dryIn=" << delayed(streamPosition + frame, latency, channel, 1.0)
                                  << " sub=" << st.submitted << " comp=" << st.completed << " to=" << st.timedOut
                                  << " busy=" << st.hostBusy << " rej=" << st.rejected << " dry=" << st.dryFrames << std::endl;
                        REQUIRE(false);
                    }
                }
            streamPosition += static_cast<std::uint64_t>(frames);
        }
        watchAllocations.store(false, std::memory_order_release);

        REQUIRE(allocationCount.load(std::memory_order_relaxed) == initialAllocations);
        const auto& stats = client.getStats();
        REQUIRE(latency == static_cast<std::uint32_t>(std::ceil(sampleRate * 0.04)));
        REQUIRE(stats.timedOut == 0);
        REQUIRE(stats.hostBusy == 0);
        REQUIRE(stats.rejected == 0);
        REQUIRE(stats.dryFrames == latency);   // only the initial silence
        REQUIRE(stats.completed == stats.submitted);
    }

    // ASIO: one small callback per hardware period from the driver thread. The
    // host is slow (each block costs `hostDelayMicros`), yet within the slack
    // the callback must not wait for it and every frame must be host output.
    void testRealtimeSmallBuffersDoNotWaitForHost(int sampleRate, int period, int hostDelayMicros)
    {
        SharedMemory memory;
        Events events;
        OutProcAudioClient client;
        REQUIRE(client.prepare());
        auto& header = *memory.header;
        FakeHost host(header, events, hostDelayMicros);
        constexpr int channels = 2;
        std::vector<std::int32_t> pcm(static_cast<std::size_t>(period) * channels);
        std::uint64_t sequence = 0;
        std::uint64_t streamPosition = 0;
        std::int64_t maxWait = 0;

        const auto start = std::chrono::steady_clock::now();
        const auto totalFrames = static_cast<std::uint64_t>(sampleRate) / 2;   // 0.5 s
        while (streamPosition < totalFrames)
        {
            fillPcm(pcm, period, channels, streamPosition);
            client.process(pcm.data(), period, 32, channels, sampleRate, &header, sequence,
                           events.request, events.response);
            maxWait = (std::max)(maxWait, client.getLastCall().maxWaitTicks);
            const auto latency = client.getLatencyFrames();
            for (int frame = 0; frame < period; ++frame)
                for (int channel = 0; channel < channels; ++channel)
                    REQUIRE(std::abs(pcmAt(pcm, frame, channels, channel)
                                     - delayed(streamPosition + frame, latency, channel, hostGain)) < 1.0e-6);
            streamPosition += static_cast<std::uint64_t>(period);
            const auto due = start + std::chrono::duration<double>(static_cast<double>(streamPosition) / sampleRate);
            while (std::chrono::steady_clock::now() < due) {}
        }

        const auto& stats = client.getStats();
        REQUIRE(stats.timedOut == 0);
        REQUIRE(stats.hostBusy == 0);
        REQUIRE(stats.dryFrames == client.getLatencyFrames());
        REQUIRE(maxWait * 1000000 / qpcFrequency() < 1000);  // never blocked the "driver" for 1 ms
    }

    void testHostNotReadyPlaysDelayedDryWithoutWaiting()
    {
        SharedMemory memory;
        Events events;
        OutProcAudioClient client;
        REQUIRE(client.prepare());
        auto& header = *memory.header;
        std::vector<std::int32_t> pcm(4096 * 2);
        std::uint64_t sequence = 0;
        std::uint64_t streamPosition = 0;

        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 20; ++i)
        {
            fillPcm(pcm, 4096, 2, streamPosition);
            REQUIRE(!client.process(pcm.data(), 4096, 32, 2, 48000, &header, sequence,
                                    events.request, events.response));
            const auto latency = client.getLatencyFrames();
            for (int frame = 0; frame < 4096; ++frame)
                REQUIRE(std::abs(pcmAt(pcm, frame, 2, 1) - delayed(streamPosition + frame, latency, 1, 1.0)) < 1.0e-6);
            streamPosition += 4096;
        }
        REQUIRE(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(50));
        REQUIRE(sequence == 0);
        REQUIRE(client.getStats().hostBusy > 0);
    }

    void testHungHostWaitsOnceThenRecovers()
    {
        SharedMemory memory;
        Events events;
        OutProcAudioClient client;
        REQUIRE(client.prepare());
        auto& header = *memory.header;
        header.hostAudioReady.store(1, std::memory_order_release);
        std::vector<std::int32_t> pcm(4800 * 2);
        std::uint64_t sequence = 0;
        std::uint64_t streamPosition = 0;

        // No host thread: output is the delayed dry signal, and at most one wait.
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 30; ++i)
        {
            fillPcm(pcm, 4800, 2, streamPosition);
            client.process(pcm.data(), 4800, 32, 2, 48000, &header, sequence,
                           events.request, events.response);
            const auto latency = client.getLatencyFrames();
            for (int frame = 0; frame < 4800; ++frame)
                REQUIRE(std::abs(pcmAt(pcm, frame, 2, 0) - delayed(streamPosition + frame, latency, 0, 1.0)) < 1.0e-6);
            streamPosition += 4800;
        }
        REQUIRE(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(200));
        REQUIRE(client.getStats().timedOut == 1);

        // Host comes back: the backlog is answered and processing resumes.
        FakeHost host(header, events, 0, false);
        SetEvent(events.request);
        bool resumed = false;
        for (int attempt = 0; attempt < 400 && !resumed; ++attempt)
        {
            fillPcm(pcm, 480, 2, streamPosition);
            resumed = client.process(pcm.data(), 480, 32, 2, 48000, &header, sequence,
                                     events.request, events.response);
            streamPosition += 480;
            Sleep(1);
        }
        REQUIRE(resumed);
        REQUIRE(client.getStats().completed > 0);
    }

    void testNonFiniteHostOutputIsRejected()
    {
        SharedMemory memory;
        Events events;
        OutProcAudioClient client;
        REQUIRE(client.prepare());
        auto& header = *memory.header;
        header.hostAudioReady.store(1, std::memory_order_release);
        std::uint64_t sequence = 0;
        std::vector<std::int32_t> pcm(4096 * 2, 1 << 28);
        std::atomic<bool> done { false };

        std::thread host([&]
        {
            while (!done.load())
            {
                WaitForSingleObject(events.request, 10);
                for (std::uint32_t i = 0; i < VST3_BRIDGE_IPC_SLOT_COUNT; ++i)
                {
                    auto& slot = header.slots[i];
                    auto expected = VST3_BRIDGE_SLOT_REQUEST;
                    if (!slot.state.compare_exchange_strong(expected, VST3_BRIDGE_SLOT_PROCESSING))
                        continue;
                    auto* output = vst3BridgeIpcSlotOutput(&header, i);
                    for (std::size_t s = 0; s < static_cast<std::size_t>(slot.numFrames) * slot.numChannels; ++s)
                        output[s] = std::numeric_limits<double>::quiet_NaN();
                    slot.status = VST3_BRIDGE_SLOT_OK;
                    slot.state.store(VST3_BRIDGE_SLOT_RESPONSE, std::memory_order_release);
                    SetEvent(events.response);
                }
            }
        });
        for (int i = 0; i < 8; ++i)
        {
            client.process(pcm.data(), 4096, 32, 2, 48000, &header, sequence, events.request, events.response);
            for (const auto sample : pcm)
                REQUIRE(std::isfinite(static_cast<double>(sample)));
        }
        done.store(true);
        host.join();
        REQUIRE(client.getStats().rejected >= 2);
        REQUIRE(client.getStats().completed == 0);
    }

    void testIntegerFormatsRoundTrip()
    {
        SharedMemory memory;
        Events events;
        OutProcAudioClient client;
        REQUIRE(client.prepare());
        client.setMinimumSlackSeconds(0.0);   // one-frame pipeline: easy to compare
        auto& header = *memory.header;
        FakeHost host(header, events);
        std::uint64_t sequence = 0;

        std::vector<std::int16_t> pcm16(1000 * 2);
        for (std::size_t i = 0; i < pcm16.size(); ++i)
            pcm16[i] = static_cast<std::int16_t>((static_cast<int>(i) * 97) % 30000 - 15000);
        const auto original16 = pcm16;
        client.process(pcm16.data(), 1000, 16, 2, 44100, &header, sequence, events.request, events.response);
        REQUIRE(client.getLatencyFrames() == 1);
        for (std::size_t i = 2; i < pcm16.size(); ++i)
            REQUIRE(std::abs(pcm16[i] - original16[i - 2] * hostGain) <= 1.0);

        std::vector<unsigned char> pcm24(700 * 2 * 3);
        for (std::size_t i = 0; i < pcm24.size(); ++i)
            pcm24[i] = static_cast<unsigned char>(i * 31);
        auto read24 = [](const std::vector<unsigned char>& bytes, std::size_t index)
        {
            std::int32_t value = bytes[index * 3] | (bytes[index * 3 + 1] << 8) | (bytes[index * 3 + 2] << 16);
            if ((value & 0x00800000) != 0)
                value |= static_cast<std::int32_t>(0xff000000);
            return value;
        };
        const auto original24 = pcm24;
        const auto lastFrame16 = original16.size() / 2 - 1;
        client.process(pcm24.data(), 700, 24, 2, 44100, &header, sequence, events.request, events.response);
        // First frame is the previous call's last frame (16-bit source, now 24-bit).
        REQUIRE(std::abs(read24(pcm24, 0) - original16[lastFrame16 * 2] * 256.0 * hostGain) <= 256.0);
        for (std::size_t i = 2; i < 1400; ++i)
            REQUIRE(std::abs(read24(pcm24, i) - read24(original24, i - 2) * hostGain) <= 1.0);
    }

    void testFallbackTransitionIsDeclicked()
    {
        SharedMemory memory;
        Events events;
        OutProcAudioClient client;
        REQUIRE(client.prepare());
        auto& header = *memory.header;
        std::uint64_t sequence = 0;
        std::uint64_t streamPosition = 0;
        std::vector<std::int32_t> pcm(480 * 2);
        std::vector<double> output;

        {
            FakeHost host(header, events);
            for (int i = 0; i < 40; ++i)
            {
                fillPcm(pcm, 480, 2, streamPosition);
                client.process(pcm.data(), 480, 32, 2, 48000, &header, sequence, events.request, events.response);
                for (int f = 0; f < 480; ++f) output.push_back(pcmAt(pcm, f, 2, 0));
                streamPosition += 480;
            }
        }
        header.hostAudioReady.store(0, std::memory_order_release);   // host gone: wet (x0.5) -> dry (x1)
        for (int i = 0; i < 20; ++i)
        {
            fillPcm(pcm, 480, 2, streamPosition);
            client.process(pcm.data(), 480, 32, 2, 48000, &header, sequence, events.request, events.response);
            for (int f = 0; f < 480; ++f) output.push_back(pcmAt(pcm, f, 2, 0));
            streamPosition += 480;
        }
        REQUIRE(client.getStats().dryFrames > client.getLatencyFrames());
        // The sine's own second difference is ~2e-6; a click would be orders larger.
        for (std::size_t i = 2 + client.getLatencyFrames(); i < output.size(); ++i)
            REQUIRE(std::abs(output[i] - 2 * output[i - 1] + output[i - 2]) < 2.0e-3);
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
    testBurstyCallbacksAreFullyProcessed(44100, 2, 0);
    testBurstyCallbacksAreFullyProcessed(48000, 2, 200);
    testBurstyCallbacksAreFullyProcessed(96000, 6, 50);
    testBurstyCallbacksAreFullyProcessed(192000, 8, 0);
    testRealtimeSmallBuffersDoNotWaitForHost(48000, 64, 300);
    testRealtimeSmallBuffersDoNotWaitForHost(192000, 128, 1500);
    testRealtimeSmallBuffersDoNotWaitForHost(192000, 2048, 6000);
    testHostNotReadyPlaysDelayedDryWithoutWaiting();
    testHungHostWaitsOnceThenRecovers();
    testNonFiniteHostOutputIsRejected();
    testIntegerFormatsRoundTrip();
    testFallbackTransitionIsDeclicked();
    std::cout << "OutProcAudioClientTests passed\n";
    return 0;
}
