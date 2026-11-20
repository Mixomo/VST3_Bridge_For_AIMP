#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>

// Shared-memory audio transport between the AIMP DSP DLL and the isolated host.
//
// Version 10 is pipelined with a fixed slack (see OutProcAudioClient.h): the
// DSP callback hands its input to the host as blocks of at most
// VST3_BRIDGE_IPC_MAX_BLOCK_FRAMES and plays the host's answer for audio it
// received ~40 ms earlier. This works both when AIMP calls the DSP in bursts
// (DirectSound/WASAPI buffering thread) and when it calls it from the ASIO
// driver callback with a hard real-time deadline. Up to
// VST3_BRIDGE_IPC_SLOT_COUNT blocks can be in flight; the host answers them in
// sequence order.
constexpr std::uint32_t VST3_BRIDGE_IPC_MAGIC = 0x33425041; // APB3
constexpr std::uint32_t VST3_BRIDGE_IPC_VERSION = 10;
constexpr std::uint32_t VST3_BRIDGE_IPC_MAX_BLOCK_FRAMES = 4096;
constexpr std::uint32_t VST3_BRIDGE_IPC_MAX_CHANNELS = 8;
constexpr std::uint32_t VST3_BRIDGE_IPC_SLOT_COUNT = 16;

constexpr std::uint32_t VST3_BRIDGE_SLOT_EMPTY = 0;
constexpr std::uint32_t VST3_BRIDGE_SLOT_WRITING = 1;
constexpr std::uint32_t VST3_BRIDGE_SLOT_REQUEST = 2;
constexpr std::uint32_t VST3_BRIDGE_SLOT_PROCESSING = 3;
constexpr std::uint32_t VST3_BRIDGE_SLOT_RESPONSE = 4;
constexpr std::uint32_t VST3_BRIDGE_SLOT_READING = 5;

// SLOT_OK: the output buffer holds the host's result for the request (the
// processed audio, or the engine's own smoothed pass-through while no plug-in
// is loaded/prepared). SLOT_BYPASS: the output buffer was not written.
constexpr std::uint32_t VST3_BRIDGE_SLOT_OK = 0;
constexpr std::uint32_t VST3_BRIDGE_SLOT_BYPASS = 1;

struct alignas(64) VST3BridgeIpcSlot
{
    std::atomic<std::uint32_t> state { VST3_BRIDGE_SLOT_EMPTY };
    std::uint32_t status = VST3_BRIDGE_SLOT_BYPASS;
    std::uint32_t numFrames = 0;
    std::uint32_t numChannels = 0;
    std::uint32_t sampleRate = 0;
    std::uint32_t hostMicros = 0; // host processing time, for diagnostics
    std::uint64_t epoch = 0;
    std::uint64_t sequence = 0;
    std::uint64_t streamSample = 0;
};

static_assert(sizeof(VST3BridgeIpcSlot) == 64);
static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
              "IPC requires lock-free 32-bit atomics");

struct alignas(64) VST3BridgeIpcHeader
{
    std::uint32_t magic = VST3_BRIDGE_IPC_MAGIC;
    std::uint32_t version = VST3_BRIDGE_IPC_VERSION;
    std::uint32_t slotCount = VST3_BRIDGE_IPC_SLOT_COUNT;
    std::uint32_t maxBlockFrames = VST3_BRIDGE_IPC_MAX_BLOCK_FRAMES;
    std::uint32_t maxChannels = VST3_BRIDGE_IPC_MAX_CHANNELS;
    volatile long requestedArchitecture = 0;
    volatile long hostArchitecture = 0;
    volatile long showGuiSequence = 0;
    volatile long handledGuiSequence = 0;
    std::atomic<std::uint64_t> sessionEpoch { 1 };
    std::atomic<std::uint32_t> pluginLatency { 0 };
    std::atomic<std::uint32_t> pluginReady { 0 };
    std::atomic<std::uint32_t> underruns { 0 };
    std::atomic<std::uint32_t> overruns { 0 };
    std::atomic<std::uint32_t> staleBlocks { 0 };
    std::atomic<std::uint32_t> processedBlocks { 0 };
    std::atomic<std::uint32_t> safeRestartRequested { 0 };
    // Set by the host while its audio thread is servicing requests. The client
    // bypasses (without waiting) while it is zero, e.g. during host start-up.
    std::atomic<std::uint32_t> hostAudioReady { 0 };
    // Diagnostics: (thread priority + 16) | 0x100 when MMCSS registration succeeded.
    std::atomic<std::uint32_t> hostThreadInfo { 0 };
    VST3BridgeIpcSlot slots[VST3_BRIDGE_IPC_SLOT_COUNT];
};

inline constexpr std::size_t vst3BridgeIpcSlotSamples()
{
    return static_cast<std::size_t>(VST3_BRIDGE_IPC_MAX_CHANNELS)
         * VST3_BRIDGE_IPC_MAX_BLOCK_FRAMES;
}

// Slot audio is planar: channel c, frame f lives at [c * numFrames + f].
inline double* vst3BridgeIpcSlotInput(VST3BridgeIpcHeader* header, std::uint32_t slotIndex)
{
    constexpr auto slotBytes = 2 * vst3BridgeIpcSlotSamples() * sizeof(double);
    auto* storage = reinterpret_cast<unsigned char*>(header) + sizeof(VST3BridgeIpcHeader);
    return reinterpret_cast<double*>(storage + static_cast<std::size_t>(slotIndex) * slotBytes);
}

inline double* vst3BridgeIpcSlotOutput(VST3BridgeIpcHeader* header, std::uint32_t slotIndex)
{
    return vst3BridgeIpcSlotInput(header, slotIndex) + vst3BridgeIpcSlotSamples();
}

inline constexpr std::size_t vst3BridgeIpcMappingSize()
{
    constexpr auto slotBytes = 2 * vst3BridgeIpcSlotSamples() * sizeof(double);
    return sizeof(VST3BridgeIpcHeader)
         + static_cast<std::size_t>(VST3_BRIDGE_IPC_SLOT_COUNT) * slotBytes;
}
