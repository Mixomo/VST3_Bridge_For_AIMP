#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "OutProcProtocol.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>

// Client side of the pipelined shared-memory transport (protocol v10).
//
// AIMP calls Winamp DSPs in two very different ways:
//  * DirectSound/WASAPI: from its buffering thread, in bursts far faster than
//    real time while it fills its output buffer;
//  * ASIO (native buffer): from inside the driver callback, once per hardware
//    period, with a hard real-time deadline of that period.
//
// The transport therefore runs the plug-in with a fixed slack of L frames
// (~40 ms). Every callback hands its input to the host (small ASIO buffers are
// grouped into larger blocks) and returns the host's output for the frames it
// received L frames earlier; a callback longer than L also waits for its own
// newest frames. In ASIO that output is normally ready, so the
// driver thread does not wait for the plug-in at all and CPU spikes up to the
// slack are absorbed. In burst mode the callback waits (bounded) only for
// whatever the host has not finished yet. Frames the host cannot deliver in
// time are replaced by the dry input on the same delayed timeline, with a
// short crossfade so a fallback never clicks.
class OutProcAudioClient
{
public:
    struct Stats
    {
        std::uint64_t submitted = 0;    // blocks sent to the host
        std::uint64_t completed = 0;    // blocks answered and applied
        std::uint64_t timedOut = 0;     // callbacks whose bounded wait expired
        std::uint64_t hostBusy = 0;     // blocks not submitted: host not ready / no free slot
        std::uint64_t rejected = 0;     // answers that were late, stale or invalid
        std::uint64_t dryFrames = 0;    // output frames that fell back to delayed dry audio
        std::uint64_t resets = 0;       // pipeline restarts (format change / DSP restart)
    };

    struct CallInfo
    {
        std::uint16_t blocks = 0;       // blocks submitted by this callback
        std::uint16_t wetBlocks = 0;    // 1 when every output frame was host output
        std::int64_t maxWaitTicks = 0;  // time spent waiting for the host
        std::uint32_t maxHostMicros = 0;
        std::uint32_t dryFrames = 0;   // output frames that were not host output
    };

    bool prepare()
    {
        if (!dry)
        {
            dry = std::make_unique<float[]>(static_cast<std::size_t>(capacity) * VST3_BRIDGE_IPC_MAX_CHANNELS);
            wet = std::make_unique<float[]>(static_cast<std::size_t>(capacity) * VST3_BRIDGE_IPC_MAX_CHANNELS);
            stamps = std::make_unique<std::uint64_t[]>(capacity);
        }
        currentSampleRate = 0;
        currentChannels = 0;
        stats = {};
        lastCall = {};
        hostStalled = false;
        prepared = true;
        return true;
    }

    // Minimum slack between submitting a frame and playing its processed
    // version. Larger values absorb larger CPU/scheduling spikes.
    void setMinimumSlackSeconds(double seconds) noexcept
    {
        minimumSlackSeconds = (std::clamp)(seconds, 0.0, 0.25);
    }

    // The next callback starts a fresh stream (new epoch, initial silence),
    // e.g. after AIMP restarted the DSP while the host was kept alive.
    void restartStream() noexcept { currentSampleRate = 0; }

    const Stats& getStats() const noexcept { return stats; }
    const CallInfo& getLastCall() const noexcept { return lastCall; }
    std::uint32_t getLatencyFrames() const noexcept { return latency; }

    // Returns true when every output frame of the callback is host output.
    bool process(void* samples, int numFrames, int bitsPerSample, int numChannels,
                 int sampleRate, VST3BridgeIpcHeader* shared, std::uint64_t& sequence,
                 HANDLE requestEvent, HANDLE responseEvent) noexcept
    {
        if (!prepared || samples == nullptr || shared == nullptr || numFrames <= 0
            || numChannels <= 0
            || numChannels > static_cast<int>(VST3_BRIDGE_IPC_MAX_CHANNELS)
            || sampleRate <= 0 || sampleRate > maxSampleRate
            || (bitsPerSample != 8 && bitsPerSample != 16
                && bitsPerSample != 24 && bitsPerSample != 32))
            return false;

        lastCall = {};
        auto* pcm = static_cast<unsigned char*>(samples);
        bool allWet = true;
        for (int offset = 0; offset < numFrames;)
        {
            const auto frames = (std::min)(numFrames - offset, static_cast<int>(maxPieceFrames));
            allWet &= processPiece(pcm + static_cast<std::size_t>(offset) * numChannels * (bitsPerSample / 8),
                                   frames, bitsPerSample, numChannels, sampleRate, shared, sequence,
                                   requestEvent, responseEvent);
            offset += frames;
        }
        lastCall.wetBlocks = allWet ? 1 : 0;
        return allWet;
    }

private:
    static constexpr int maxSampleRate = 768000;
    static constexpr std::uint32_t maxPieceFrames = 16384;
    static constexpr std::uint32_t capacity = 1u << 17;          // ring frames
    static constexpr std::uint32_t maxPluginDelay = 32768;
    static constexpr std::uint32_t maxLatency = 48 * 1024;
    static constexpr int declickFrames = 64;

    static_assert(maxLatency + maxPluginDelay + maxPieceFrames
                  + VST3_BRIDGE_IPC_MAX_BLOCK_FRAMES < capacity);

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
        if (!std::isfinite(sample))
            sample = 0.0;
        sample = (std::clamp)(sample, -1.0, 1.0);
        if (bitsPerSample == 8)
        {
            bytes[index] = static_cast<unsigned char>((std::clamp)(
                static_cast<int>(std::lround(sample * 127.0 + 128.0)), 0, 255));
        }
        else if (bitsPerSample == 16)
        {
            const auto value = static_cast<std::int16_t>((std::clamp)(
                std::lround(sample * 32767.0), -32768L, 32767L));
            std::memcpy(bytes + index * 2, &value, sizeof(value));
        }
        else if (bitsPerSample == 24)
        {
            const auto value = static_cast<std::int32_t>((std::clamp)(
                std::lround(sample * 8388607.0), -8388608L, 8388607L));
            auto* destination = bytes + index * 3;
            destination[0] = static_cast<unsigned char>(value & 0xff);
            destination[1] = static_cast<unsigned char>((value >> 8) & 0xff);
            destination[2] = static_cast<unsigned char>((value >> 16) & 0xff);
        }
        else
        {
            const auto value = static_cast<std::int32_t>((std::clamp)(
                std::llround(sample * 2147483647.0), -2147483647LL - 1, 2147483647LL));
            std::memcpy(bytes + index * 4, &value, sizeof(value));
        }
    }

    static std::int64_t nowTicks() noexcept
    {
        LARGE_INTEGER ticks {};
        QueryPerformanceCounter(&ticks);
        return ticks.QuadPart;
    }

    static std::int64_t ticksPerSecond() noexcept
    {
        static const std::int64_t frequency = []
        {
            LARGE_INTEGER value {};
            QueryPerformanceFrequency(&value);
            return value.QuadPart > 0 ? value.QuadPart : 1;
        }();
        return frequency;
    }

    float& dryAt(std::uint64_t position, int channel) noexcept
    {
        return dry[static_cast<std::size_t>(channel) * capacity + position % capacity];
    }

    float& wetAt(std::uint64_t position, int channel) noexcept
    {
        return wet[static_cast<std::size_t>(channel) * capacity + position % capacity];
    }

    // Stamps hold position + 1 so that zero-initialised memory never matches.
    bool wetReady(std::uint64_t position) const noexcept
    {
        return stamps[position % capacity] == position + 1;
    }

    void reset(VST3BridgeIpcHeader* shared, int sampleRate, int channels)
    {
        ++stats.resets;
        currentSampleRate = sampleRate;
        currentChannels = channels;
        const auto slack = static_cast<std::uint32_t>(std::ceil(sampleRate * minimumSlackSeconds));
        latency = (std::clamp)(slack, 1u, maxLatency);
        blockTarget = (std::clamp)(latency / 4, 64u, VST3_BRIDGE_IPC_MAX_BLOCK_FRAMES);
        position = latency;     // the first `latency` output frames are silence
        pendingStart = position;
        std::memset(dry.get(), 0, sizeof(float) * capacity * VST3_BRIDGE_IPC_MAX_CHANNELS);
        std::memset(stamps.get(), 0, sizeof(std::uint64_t) * capacity);
        blend = 0.0;
        declickRemaining = 0;
        lastOut.fill(0.0);
        declickOffset.fill(0.0);
        hostStalled = false;
        realtimePace = 0.0;
        windowStartTicks = 0;
        windowFrames = 0;
        epoch = shared->sessionEpoch.fetch_add(1, std::memory_order_acq_rel) + 1;
    }

    void collect(VST3BridgeIpcHeader* shared) noexcept
    {
        for (std::uint32_t i = 0; i < VST3_BRIDGE_IPC_SLOT_COUNT; ++i)
        {
            auto& slot = shared->slots[i];
            auto expected = VST3_BRIDGE_SLOT_RESPONSE;
            if (!slot.state.compare_exchange_strong(expected, VST3_BRIDGE_SLOT_READING,
                                                    std::memory_order_acquire))
                continue;

            hostStalled = false;
            lastCall.maxHostMicros = (std::max)(lastCall.maxHostMicros, slot.hostMicros);
            const auto frames = slot.numFrames;
            const auto start = slot.streamSample;
            bool valid = slot.status == VST3_BRIDGE_SLOT_OK
                && slot.epoch == epoch
                && slot.numChannels == static_cast<std::uint32_t>(currentChannels)
                && frames > 0 && frames <= VST3_BRIDGE_IPC_MAX_BLOCK_FRAMES
                && start + frames <= position
                && start + frames + capacity > position + maxPieceFrames;
            const auto* output = vst3BridgeIpcSlotOutput(shared, i);
            const auto count = static_cast<std::size_t>(frames) * currentChannels;
            for (std::size_t s = 0; valid && s < count; ++s)
                valid = std::isfinite(output[s]);

            if (valid)
            {
                for (std::uint32_t f = 0; f < frames; ++f)
                {
                    for (int c = 0; c < currentChannels; ++c)
                        wetAt(start + f, c) = static_cast<float>(
                            output[static_cast<std::size_t>(c) * frames + f]);
                    stamps[(start + f) % capacity] = start + f + 1;
                }
                ++stats.completed;
            }
            else
            {
                ++stats.rejected;
                shared->staleBlocks.fetch_add(1, std::memory_order_relaxed);
            }
            slot.state.store(VST3_BRIDGE_SLOT_EMPTY, std::memory_order_release);
        }
    }

    // True while a request covering [from, to) is still queued or processing.
    bool inFlight(const VST3BridgeIpcHeader* shared, std::uint64_t from, std::uint64_t to) const noexcept
    {
        for (const auto& slot : shared->slots)
        {
            const auto state = slot.state.load(std::memory_order_acquire);
            if ((state == VST3_BRIDGE_SLOT_REQUEST || state == VST3_BRIDGE_SLOT_PROCESSING
                 || state == VST3_BRIDGE_SLOT_RESPONSE)
                && slot.epoch == epoch
                && slot.streamSample < to && slot.streamSample + slot.numFrames > from)
                return true;
        }
        return false;
    }

    bool allReady(std::uint64_t from, std::uint64_t to) const noexcept
    {
        for (auto p = from; p < to; ++p)
            if (!wetReady(p))
                return false;
        return true;
    }

    void submit(VST3BridgeIpcHeader* shared, std::uint64_t start, int frames,
                int sampleRate, std::uint64_t& sequence, HANDLE requestEvent) noexcept
    {
        const bool hostReady = shared->hostAudioReady.load(std::memory_order_acquire) != 0;
        bool signalled = false;
        for (int offset = 0; offset < frames;)
        {
            const auto blockFrames = (std::min)(frames - offset,
                                                static_cast<int>(VST3_BRIDGE_IPC_MAX_BLOCK_FRAMES));
            VST3BridgeIpcSlot* slot = nullptr;
            std::uint32_t slotIndex = 0;
            for (std::uint32_t i = 0; hostReady && i < VST3_BRIDGE_IPC_SLOT_COUNT; ++i)
            {
                auto expected = VST3_BRIDGE_SLOT_EMPTY;
                if (shared->slots[i].state.compare_exchange_strong(expected, VST3_BRIDGE_SLOT_WRITING,
                                                                   std::memory_order_acquire))
                {
                    slot = &shared->slots[i];
                    slotIndex = i;
                    break;
                }
            }
            if (slot == nullptr)
            {
                ++stats.hostBusy;
                if (hostReady)
                    shared->overruns.fetch_add(1, std::memory_order_relaxed);
                offset += blockFrames;
                continue;
            }

            auto* input = vst3BridgeIpcSlotInput(shared, slotIndex);
            const auto blockStart = start + static_cast<std::uint64_t>(offset);
            for (int c = 0; c < currentChannels; ++c)
                for (int f = 0; f < blockFrames; ++f)
                    input[static_cast<std::size_t>(c) * blockFrames + f] = dryAt(blockStart + f, c);

            slot->status = VST3_BRIDGE_SLOT_BYPASS;
            slot->numFrames = static_cast<std::uint32_t>(blockFrames);
            slot->numChannels = static_cast<std::uint32_t>(currentChannels);
            slot->sampleRate = static_cast<std::uint32_t>(sampleRate);
            slot->hostMicros = 0;
            slot->epoch = epoch;
            slot->sequence = ++sequence;
            slot->streamSample = blockStart;
            slot->state.store(VST3_BRIDGE_SLOT_REQUEST, std::memory_order_release);
            ++stats.submitted;
            ++lastCall.blocks;
            signalled = true;
            offset += blockFrames;
        }
        if (signalled && requestEvent != nullptr)
            SetEvent(requestEvent);
    }

    // Waits until the host has answered every frame in [from, to) that is still
    // in flight, or until the budget is spent. Sub-millisecond budgets (small
    // ASIO buffers) spin instead of sleeping.
    void waitFor(VST3BridgeIpcHeader* shared, std::uint64_t from, std::uint64_t to,
                 double budgetSeconds, HANDLE responseEvent) noexcept
    {
        collect(shared);
        if (hostStalled || allReady(from, to) || !inFlight(shared, from, to))
            return;

        const auto frequency = ticksPerSecond();
        const auto begin = nowTicks();
        const auto deadline = begin + static_cast<std::int64_t>(budgetSeconds * frequency);
        for (;;)
        {
            const auto now = nowTicks();
            const auto remaining = deadline - now;
            if (remaining <= 0)
            {
                ++stats.timedOut;
                hostStalled = true;  // do not wait again until the host answers something
                break;
            }
            const auto remainingMs = remaining * 1000 / frequency;
            if (remainingMs >= 2 && responseEvent != nullptr)
                WaitForSingleObject(responseEvent, static_cast<DWORD>(remainingMs - 1));
            else
                YieldProcessor();
            collect(shared);
            if (allReady(from, to) || !inFlight(shared, from, to))
                break;
        }
        lastCall.maxWaitTicks = (std::max)(lastCall.maxWaitTicks, nowTicks() - begin);
    }

    // A real-time caller (ASIO driver callback) must never be held past part of
    // its period; a caller running ahead of real time (AIMP filling its buffer)
    // can afford to wait for the host. Pacing is measured as wall time between
    // callbacks relative to the audio duration they delivered.
    double waitBudgetSeconds(int frames, int sampleRate) noexcept
    {
        const auto now = nowTicks();
        if (windowStartTicks == 0)
            windowStartTicks = now;
        else if (windowFrames >= static_cast<std::uint64_t>(sampleRate) / 50)   // >= 20 ms of audio
        {
            const auto wall = static_cast<double>(now - windowStartTicks) / ticksPerSecond();
            const auto audio = static_cast<double>(windowFrames) / sampleRate;
            realtimePace = wall / audio;
            windowStartTicks = now;
            windowFrames = 0;
        }
        windowFrames += static_cast<std::uint64_t>(frames);
        const auto period = static_cast<double>(frames) / sampleRate;
        return realtimePace > 0.5 ? 0.5 * period : (std::max)(0.75 * period, 0.02);
    }

    bool processPiece(unsigned char* pcm, int frames, int bitsPerSample, int channels,
                      int sampleRate, VST3BridgeIpcHeader* shared, std::uint64_t& sequence,
                      HANDLE requestEvent, HANDLE responseEvent) noexcept
    {
        const bool headerValid = shared->magic == VST3_BRIDGE_IPC_MAGIC
                              && shared->version == VST3_BRIDGE_IPC_VERSION
                              && shared->maxBlockFrames == VST3_BRIDGE_IPC_MAX_BLOCK_FRAMES
                              && shared->maxChannels == VST3_BRIDGE_IPC_MAX_CHANNELS;
        const auto pieceFrames = static_cast<std::uint32_t>(frames);
        if (sampleRate != currentSampleRate || channels != currentChannels)
            reset(shared, sampleRate, channels);

        // 1. Store the input on the dry timeline. Once a block's worth has
        //    accumulated, hand it to the host straight away.
        const auto inputStart = position;
        for (int f = 0; f < frames; ++f)
            for (int c = 0; c < channels; ++c)
                dryAt(inputStart + f, c) = static_cast<float>(readPcmSample(
                    pcm, bitsPerSample, static_cast<std::size_t>(f) * channels + c));
        position += pieceFrames;

        // 2. The output of this callback is the host's answer for the frames
        //    received `latency` frames earlier.
        const auto outStart = inputStart - latency;
        const auto outEnd = outStart + pieceFrames;
        if (position - pendingStart >= blockTarget || outEnd > pendingStart)
        {
            if (headerValid)
                submit(shared, pendingStart, static_cast<int>(position - pendingStart),
                       sampleRate, sequence, requestEvent);
            pendingStart = position;
        }
        if (headerValid)
            waitFor(shared, outStart, outEnd, waitBudgetSeconds(frames, sampleRate), responseEvent);

        const auto pluginDelay = (std::min)(shared->pluginLatency.load(std::memory_order_acquire),
                                            maxPluginDelay);
        const auto fadeStep = 1.0 / declickFrames;
        bool allWet = true;
        for (int f = 0; f < frames; ++f)
        {
            const auto p = outStart + static_cast<std::uint64_t>(f);
            const bool ready = wetReady(p);
            allWet &= ready;
            // Dry fallback is aligned with the wet signal (pipeline + plug-in delay).
            const bool dryKnown = p >= pluginDelay;
            // The first real frame (after the initial silence) starts in
            // whichever state applies, without a transition.
            if (p == latency)
                blend = ready ? 1.0 : 0.0;
            if (!ready && blend > 0.0)
            {
                // Wet -> dry: start a de-click ramp from the last wet output.
                blend = 0.0;
                declickRemaining = declickFrames;
                for (int c = 0; c < channels; ++c)
                    declickOffset[static_cast<std::size_t>(c)] =
                        lastOut[static_cast<std::size_t>(c)]
                        - (dryKnown ? dryAt(p - pluginDelay, c) : 0.0f);
            }
            if (ready)
                blend = (std::min)(1.0, blend + fadeStep);
            else
            {
                ++stats.dryFrames;
                ++lastCall.dryFrames;
            }

            const auto ramp = declickRemaining > 0
                ? static_cast<double>(declickRemaining) / (declickFrames + 1) : 0.0;
            for (int c = 0; c < channels; ++c)
            {
                const double drySample = dryKnown ? dryAt(p - pluginDelay, c) : 0.0;
                double out = ready ? drySample + (wetAt(p, c) - drySample) * blend : drySample;
                out += declickOffset[static_cast<std::size_t>(c)] * ramp;
                lastOut[static_cast<std::size_t>(c)] = out;
                writePcmSample(pcm, bitsPerSample, static_cast<std::size_t>(f) * channels + c, out);
            }
            if (declickRemaining > 0)
                --declickRemaining;
        }

        if (!allWet)
            shared->underruns.fetch_add(1, std::memory_order_relaxed);
        return allWet;
    }

    bool prepared = false;
    int currentSampleRate = 0;
    int currentChannels = 0;
    std::uint32_t latency = 0;
    std::uint32_t blockTarget = 64;
    std::uint64_t pendingStart = 0;
    std::uint64_t position = 0;
    std::uint64_t epoch = 0;
    bool hostStalled = false;
    double minimumSlackSeconds = 0.04;
    double blend = 0.0;
    double realtimePace = 0.0;   // wall time / audio time; ~1 for ASIO, << 1 when buffering ahead
    std::int64_t windowStartTicks = 0;
    std::uint64_t windowFrames = 0;
    int declickRemaining = 0;
    std::array<double, VST3_BRIDGE_IPC_MAX_CHANNELS> lastOut {};
    std::array<double, VST3_BRIDGE_IPC_MAX_CHANNELS> declickOffset {};
    std::unique_ptr<float[]> dry;
    std::unique_ptr<float[]> wet;
    std::unique_ptr<std::uint64_t[]> stamps;
    Stats stats;
    CallInfo lastCall;
};
