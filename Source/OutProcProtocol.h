#pragma once

#include <cstddef>
#include <cstdint>

constexpr std::uint32_t VST3_BRIDGE_IPC_MAGIC = 0x33425041; // APB3
constexpr std::uint32_t VST3_BRIDGE_IPC_VERSION = 3;
constexpr std::uint32_t VST3_BRIDGE_IPC_MAX_AUDIO_BYTES = 32 * 1024 * 1024;
constexpr std::uint32_t VST3_BRIDGE_IPC_SLOT_COUNT = 4;
constexpr std::uint64_t VST3_BRIDGE_IPC_PIPELINE_BLOCKS = 2;

constexpr LONG VST3_BRIDGE_SLOT_EMPTY = 0;
constexpr LONG VST3_BRIDGE_SLOT_REQUEST = 1;
constexpr LONG VST3_BRIDGE_SLOT_RESPONSE = 2;

struct VST3BridgeIpcSlot
{
    volatile LONG state;
    std::uint32_t status;
    std::uint32_t audioBytes;
    std::uint32_t numFrames;
    std::uint32_t bitsPerSample;
    std::uint32_t numChannels;
    std::uint32_t sampleRate;
    std::uint64_t sequence;
};

struct VST3BridgeIpcHeader
{
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t slotCount;
    std::uint32_t maxAudioBytes;
    VST3BridgeIpcSlot slots[VST3_BRIDGE_IPC_SLOT_COUNT];
};

inline unsigned char* vst3BridgeIpcAudio(VST3BridgeIpcHeader* header, std::uint32_t slotIndex)
{
    return reinterpret_cast<unsigned char*>(header + 1)
         + static_cast<std::size_t>(slotIndex) * VST3_BRIDGE_IPC_MAX_AUDIO_BYTES;
}

inline constexpr std::size_t vst3BridgeIpcMappingSize()
{
    return sizeof(VST3BridgeIpcHeader)
         + static_cast<std::size_t>(VST3_BRIDGE_IPC_SLOT_COUNT) * VST3_BRIDGE_IPC_MAX_AUDIO_BYTES;
}
