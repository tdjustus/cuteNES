#pragma once

#include "cutenes/bus/bus.hpp"
#include "cutenes/system/region.hpp"
#include "cutenes/system/scheduler.hpp"

#include <cutenes/cutenes.hpp>

#include <array>
#include <cstdint>

namespace cutenes {

// Private implementation detail of cutenes::Machine (pimpl). Not installed,
// not part of the public contract.
struct Machine::Impl {
    explicit Impl(const MachineConfig& config);

    void deliverFrame();
    void deliverAudioForTicks(std::uint64_t ticks);

    MachineConfig config;
    Region regionId;
    const RegionProfile& profile;
    Scheduler scheduler;
    Bus bus;

    IVideoSink* videoSink = nullptr;
    IAudioSink* audioSink = nullptr;
    std::array<std::uint32_t, 256 * 240> framebuffer{};
    std::array<std::int16_t, 4096> audioChunk{};

    // Lossless fixed-point audio accounting (see deliverAudioForTicks).
    std::uint64_t audioNumerator = 0;

    RunState runState = RunState::NeedsRom;
    CartridgeInfo info{};
    CpuState cpu{};
    ButtonSet controllers[2]{};
};

} // namespace cutenes
