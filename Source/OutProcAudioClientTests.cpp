#include "OutProcAudioClient.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>

namespace
{
    struct alignas(64) SharedMemory
    {
        VST3BridgeIpcHeader header;
        std::array<std::byte, vst3BridgeIpcMappingSize() - sizeof(VST3BridgeIpcHeader)> audio {};
    };
}

int main()
{
    auto shared = std::make_unique<SharedMemory>();
    OutProcAudioClient client;
    assert(client.prepare());

    std::array<std::int16_t, 1152 * 2> callback {};
    for (std::size_t i = 0; i < callback.size(); ++i)
        callback[i] = static_cast<std::int16_t>(static_cast<int>(i % 2048) - 1024);

    std::uint64_t sequence = 0;
    assert(client.process(callback.data(), 1152, 16, 2, 44100, &shared->header, sequence));
    assert(sequence == 2);

    std::uint32_t requests = 0;
    for (std::uint32_t i = 0; i < VST3_BRIDGE_IPC_SLOT_COUNT; ++i)
    {
        const auto& slot = shared->header.slots[i];
        if (slot.state.load(std::memory_order_acquire) != VST3_BRIDGE_SLOT_REQUEST)
            continue;
        ++requests;
        assert(slot.numFrames == VST3_BRIDGE_IPC_QUANTUM_FRAMES);
        assert(slot.numChannels == 2);
        assert(slot.sampleRate == 44100);
        assert(slot.epoch == shared->header.sessionEpoch.load(std::memory_order_acquire));
    }
    assert(requests == 2);

    const auto oldEpoch = shared->header.sessionEpoch.load(std::memory_order_acquire);
    std::array<std::int16_t, 512 * 2> changedRateCallback {};
    assert(client.process(changedRateCallback.data(), 512, 16, 2, 96000,
                          &shared->header, sequence));
    assert(shared->header.sessionEpoch.load(std::memory_order_acquire) > oldEpoch);

    std::cout << "OutProcAudioClientTests passed\n";
    return 0;
}
