#include "cutenes/system/machine_impl.hpp"

#include <cutenes/cutenes.hpp>

#include <array>
#include <utility>

namespace cutenes {

namespace {

constexpr std::size_t kAudioChunkMax = 4096; // contracts/media-output.md bound

} // namespace

Machine::Impl::Impl(const MachineConfig& cfg)
    : config(cfg)
    , regionId(Region::Ntsc)
    , profile(regionProfile(regionId))
    , scheduler(profile)
{
}

void Machine::Impl::deliverFrame()
{
    if (videoSink == nullptr) {
        return;
    }
    // framebuffer stays zero-filled (black, bits 24-31 zero) until the PPU
    // renders real output — task T021/T026.
    const VideoFrame frame{framebuffer.data(), scheduler.frameIndex()};
    videoSink->onFrame(frame);
}

void Machine::Impl::deliverAudioForTicks(std::uint64_t ticks)
{
    if (audioSink == nullptr) {
        return;
    }
    // Deterministic fixed-point sample accounting: samples for an interval =
    // sampleRate * ticks / masterRate, accumulated without loss
    // (contracts/media-output.md; real samples come from the APU, task T024).
    audioNumerator += static_cast<std::uint64_t>(config.audioSampleRate)
        * ticks * profile.masterClockDen;
    std::uint64_t pending = audioNumerator / profile.masterClockNum;
    audioNumerator %= profile.masterClockNum;

    while (pending > 0) {
        const std::size_t count = static_cast<std::size_t>(
            pending > kAudioChunkMax ? kAudioChunkMax : pending);
        audioSink->onSamples(audioChunk.data(), count, scheduler.now());
        pending -= count;
    }
}

Machine::Machine(const MachineConfig& config)
    : m_impl(std::make_unique<Impl>(config))
{
}

Machine::~Machine() = default;

Machine::Machine(Machine&&) noexcept = default;
Machine& Machine::operator=(Machine&&) noexcept = default;

LoadResult Machine::loadCartridge(const std::uint8_t* /*data*/,
    std::size_t /*size*/)
{
    // iNES 1.0 / NES 2.0 parsing lands with task T018 (US1).
    return LoadResult{LoadError::NotAnInesFile, 0, CartridgeInfo{}};
}

void Machine::ejectCartridge()
{
    m_impl->info = CartridgeInfo{};
    m_impl->runState = RunState::NeedsRom;
}

const CartridgeInfo& Machine::cartridgeInfo() const { return m_impl->info; }

void Machine::powerOn()
{
    auto& impl = *m_impl;
    // Cold boot per research.md D4: A=X=Y=0, S=$FD, P=$24 (bit 5 reads 1, B
    // flag never materialized). PC from the reset vector.
    impl.cpu = CpuState{};
    impl.cpu.pc = static_cast<std::uint16_t>(
        impl.bus.read(0xFFFC) | (std::uint16_t{impl.bus.read(0xFFFD)} << 8));

    // The reset sequence consumes 7 CPU cycles with the PPU ticking, so
    // CYC:7 / PPU (0,21) matches nestest.log line 1 exactly (research.md D4).
    // Real CPU/PPU execution arrives with tasks T019/T021.
    impl.scheduler = Scheduler(impl.profile);
    impl.scheduler.advanceMaster(7 * impl.profile.cpuDivisor);

    impl.runState =
        impl.info.prgSize > 0 ? RunState::Paused : RunState::NeedsRom;
}

void Machine::reset()
{
    auto& impl = *m_impl;
    // Reset-button behavior (research.md D4): S -= 3, I flag set, registers
    // otherwise preserved, PC re-fetched. $4015 clear / $4011 &= 1 are APU
    // side effects applied when the APU attaches (task T036).
    impl.cpu.s = static_cast<std::uint8_t>(impl.cpu.s - 3);
    impl.cpu.p |= 0x04; // I flag
    impl.cpu.pc = static_cast<std::uint16_t>(
        impl.bus.read(0xFFFC) | (std::uint16_t{impl.bus.read(0xFFFD)} << 8));
    impl.scheduler.advanceMaster(7 * impl.profile.cpuDivisor);
}

void Machine::runFrames(std::uint32_t frames)
{
    auto& impl = *m_impl;
    for (std::uint32_t i = 0; i < frames; ++i) {
        const std::uint64_t before = impl.scheduler.now();
        impl.scheduler.endFrame(); // advance to the exact frame boundary
        impl.deliverAudioForTicks(impl.scheduler.now() - before);
        impl.deliverFrame();
    }
}

void Machine::frameAdvance()
{
    runFrames(1);
}

void Machine::stepInstruction()
{
    // Placeholder granularity (one CPU cycle) until the instruction-stepped
    // CPU lands with tasks T019/T040.
    m_impl->scheduler.advanceMaster(m_impl->profile.cpuDivisor);
}

void Machine::pause()
{
    if (m_impl->runState == RunState::Running) {
        m_impl->runState = RunState::Paused;
    }
}

void Machine::resume()
{
    if (m_impl->runState == RunState::Paused) {
        m_impl->runState = RunState::Running;
    }
}

RunState Machine::runState() const { return m_impl->runState; }

void Machine::setController(Port port, ButtonSet buttons)
{
    // Serial polling behavior arrives with task T044; the machine stores the
    // raw state now so the API is complete.
    const std::size_t index = port == Port::Two ? 1 : 0;
    m_impl->controllers[index] =
        buttons & ButtonSet{0xFF}; // full replace, 8 defined bits
}

ButtonSet Machine::controller(Port port) const
{
    const std::size_t index = port == Port::Two ? 1 : 0;
    return m_impl->controllers[index];
}

void Machine::setVideoSink(IVideoSink* sink) { m_impl->videoSink = sink; }
void Machine::setAudioSink(IAudioSink* sink) { m_impl->audioSink = sink; }

CpuState Machine::cpu() const
{
    CpuState state = m_impl->cpu;
    state.masterCycle = m_impl->scheduler.now();
    return state;
}

void Machine::readMemory(std::uint16_t addr, std::size_t len,
    std::uint8_t* dst) const
{
    for (std::size_t i = 0; i < len; ++i) {
        // Note: reads carry register side effects (e.g. $2002 clears VBlank)
        // once those registers exist (tasks T021/T023).
        const std::uint16_t a = static_cast<std::uint16_t>(addr + i);
        dst[i] = const_cast<Bus&>(m_impl->bus).read(a);
    }
}

WriteError Machine::writeMemory(std::uint16_t addr, const std::uint8_t* src,
    std::size_t len)
{
    for (std::size_t i = 0; i < len; ++i) {
        const std::uint16_t a = static_cast<std::uint16_t>(addr + i);
        m_impl->bus.write(a, src[i]);
    }
    return WriteError::Ok;
}

WriteError Machine::writeCpuRegister(std::uint16_t addr, std::uint8_t value)
{
    if (addr < 0x2000 || addr > 0x401F) {
        return WriteError::InvalidAddress;
    }
    m_impl->bus.write(addr, value);
    return WriteError::Ok;
}

SaveStateBlob Machine::captureState()
{
    // Full serialization lands with tasks T048/T049 (US5).
    return SaveStateBlob{};
}

LoadStateError Machine::loadState(const std::uint8_t* /*data*/,
    std::size_t size)
{
    // Rejection matrix lands with tasks T047-T049 (US5); until then any
    // request is refused rather than guessed at.
    return size == 0 ? LoadStateError::BadMagic
                     : LoadStateError::IncompatibleFormat;
}

SramRange Machine::sram() const
{
    // Battery SRAM surfaces when cartridges parse (tasks T018/T050).
    return SramRange{};
}

void Machine::loadSram(const std::uint8_t* /*data*/, std::size_t /*size*/)
{
    // Size resolution (header flags-8, NES 2.0 log encoding) is task T050.
}

Region Machine::region() const { return m_impl->regionId; }

std::uint64_t Machine::frameCounter() const
{
    return m_impl->scheduler.frameIndex();
}

std::uint64_t Machine::masterCycle() const { return m_impl->scheduler.now(); }

} // namespace cutenes
