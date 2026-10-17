#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>

constexpr std::uint32_t VST3_BRIDGE_IPC_MAGIC = 0x33425041; // APB3
constexpr std::uint32_t VST3_BRIDGE_IPC_VERSION = 8;
constexpr std::uint32_t VST3_BRIDGE_IPC_QUANTUM_FRAMES = 512;
constexpr std::uint32_t VST3_BRIDGE_IPC_MAX_CHANNELS = 8;
constexpr std::uint32_t VST3_BRIDGE_IPC_SLOT_COUNT = 4;

constexpr std::uint32_t VST3_BRIDGE_SLOT_EMPTY = 0;
constexpr std::uint32_t VST3_BRIDGE_SLOT_WRITING = 1;
constexpr std::uint32_t VST3_BRIDGE_SLOT_REQUEST = 2;
constexpr std::uint32_t VST3_BRIDGE_SLOT_PROCESSING = 3;
constexpr std::uint32_t VST3_BRIDGE_SLOT_RESPONSE = 4;
constexpr std::uint32_t VST3_BRIDGE_SLOT_READING = 5;
constexpr std::uint32_t VST3_BRIDGE_SLOT_OK = 0;
constexpr std::uint32_t VST3_BRIDGE_SLOT_BYPASS = 1;

struct alignas(64) VST3BridgeIpcSlot
{
    std::atomic<std::uint32_t> state { VST3_BRIDGE_SLOT_EMPTY };
    std::uint32_t status = VST3_BRIDGE_SLOT_BYPASS;
    std::uint32_t numFrames = 0;
    std::uint32_t numChannels = 0;
    std::uint32_t sampleRate = 0;
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
    std::uint32_t quantumFrames = VST3_BRIDGE_IPC_QUANTUM_FRAMES;
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
    VST3BridgeIpcSlot slots[VST3_BRIDGE_IPC_SLOT_COUNT];
};

inline constexpr std::size_t vst3BridgeIpcSlotSamples()
{
    return static_cast<std::size_t>(VST3_BRIDGE_IPC_MAX_CHANNELS)
         * VST3_BRIDGE_IPC_QUANTUM_FRAMES;
}

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

inline unsigned char* vst3BridgeIpcAudio(VST3BridgeIpcHeader* header, std::uint32_t slotIndex)
{
    return reinterpret_cast<unsigned char*>(vst3BridgeIpcSlotInput(header, slotIndex));
}

inline constexpr std::size_t vst3BridgeIpcMappingSize()
{
    constexpr auto slotBytes = sizeof(VST3BridgeIpcSlot)
        + 2 * vst3BridgeIpcSlotSamples() * sizeof(double);
    return sizeof(VST3BridgeIpcHeader)
         + static_cast<std::size_t>(VST3_BRIDGE_IPC_SLOT_COUNT) * slotBytes;
}
