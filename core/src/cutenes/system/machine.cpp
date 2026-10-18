// Machine facade — wires CPU, PPU, APU, cartridge, and the run loop
// (tasks T010/T011 skeleton superseded by this integration; tasks
// T019-T026 components).

#include "cutenes/system/machine_impl.hpp"

#include <cstring>
#include <utility>

namespace cutenes {

Machine::Impl::Impl(const MachineConfig& cfg)
    : config(cfg)
{
    buildComponents(Region::Ntsc);
}

void Machine::Impl::buildComponents(Region region)
{
    regionId = region;
    profile = &regionProfile(region);
    scheduler = Scheduler{*profile};

    ppu = std::make_unique<Ppu>(*profile, scheduler);
    apu = std::make_unique<Apu>(*profile, scheduler, bus);
    cpu = std::make_unique<Cpu>(bus, scheduler, ppu.get(), apu.get());

    ppu->setVideoOutput(framebuffer.data());
    ppu->setNmiCallback([this] { cpu->assertNmi(); });
    apu->setOutputRate(config.audioSampleRate);

    io = ApuIoBridge{};
    io.sched = &scheduler;
    io.bus = &bus;
    io.ppu = ppu.get();
    io.apu = apu.get();

    bus.attachPpu(ppu.get());
    bus.attachApuIo(&io);
    bus.attachCartridge(&cart);
}

void Machine::Impl::deliverFrame()
{
    if (videoSink != nullptr) {
        const VideoFrame frame{framebuffer.data(), frameCounter};
        videoSink->onFrame(frame);
    }
}

// Execute instructions until the PPU emits its final visible pixel, keeping
// every component caught up to the master clock along the way.
void Machine::Impl::runUntilFrameComplete()
{
    while (true) {
        cpu->setIrq(apu->frameIrqPending() || apu->dmcIrqPending());
        cpu->step();
        ppu->runTo(scheduler.now());
        if (ppu->takeFrameReady()) {
            apu->runTo(scheduler.now());
            apu->flushToSink();
            deliverFrame();
            ++frameCounter;
            return;
        }
    }
}

Machine::Machine(const MachineConfig& config)
    : m_impl(std::make_unique<Impl>(config))
{
}

Machine::~Machine() = default;

Machine::Machine(Machine&&) noexcept = default;
Machine& Machine::operator=(Machine&&) noexcept = default;

LoadResult Machine::loadCartridge(const std::uint8_t* data, std::size_t size)
{
    auto& impl = *m_impl;
    const LoadResult result =
        Cartridge::load(data, size, impl.cart);
    if (result.error != LoadError::Ok) {
        impl.runState = RunState::NeedsRom;
        return result;
    }

    // Region resolves from the header (NES 2.0) with the NTSC default.
    // A region change rebuilds every timing-dependent component.
    const Region region =
        result.info.region == Region::Pal ? Region::Pal : Region::Ntsc;
    if (region != impl.regionId) {
        const MachineConfig config = impl.config;
        impl = Impl(config);
        // Re-parse into the rebuilt implementation.
        const LoadResult again = Cartridge::load(data, size, impl.cart);
        (void)again;
    }
    impl.ppu->attachCartridge(&impl.cart);
    impl.runState = RunState::Paused;
    return result;
}

void Machine::ejectCartridge()
{
    auto& impl = *m_impl;
    impl.cart = Cartridge{};
    impl.ppu->attachCartridge(nullptr);
    impl.runState = RunState::NeedsRom;
}

const CartridgeInfo& Machine::cartridgeInfo() const
{
    return m_impl->cart.info;
}

void Machine::powerOn()
{
    auto& impl = *m_impl;
    // Cold boot: zero the sole time source, reset the passive components,
    // then the CPU last — its 7-cycle reset drives PPU/APU catch-up, so the
    // machine ends at CYC:7 / PPU (0,21) exactly (nestest.log line 1).
    impl.scheduler.reset();
    impl.ppu->reset(/*cold=*/true);
    impl.apu->reset(/*cold=*/true);
    impl.cpu->reset(/*cold=*/true);
    impl.frameCounter = 0;
    impl.runState =
        impl.cart.info.prgSize > 0 ? RunState::Paused : RunState::NeedsRom;
}

void Machine::reset()
{
    m_impl->cpu->reset(/*cold=*/false);
    m_impl->ppu->reset(/*cold=*/false);
    m_impl->apu->reset(/*cold=*/false);
}

void Machine::runFrames(std::uint32_t frames)
{
    for (std::uint32_t i = 0; i < frames; ++i) {
        m_impl->runUntilFrameComplete();
    }
}

void Machine::frameAdvance() { runFrames(1); }

void Machine::stepInstruction()
{
    auto& impl = *m_impl;
    impl.cpu->setIrq(impl.apu->frameIrqPending() || impl.apu->dmcIrqPending());
    impl.cpu->step();
    impl.ppu->runTo(impl.scheduler.now());
    impl.apu->runTo(impl.scheduler.now());
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
    const std::size_t index = port == Port::Two ? 1 : 0;
    m_impl->controllers[index] = buttons;
}

ButtonSet Machine::controller(Port port) const
{
    const std::size_t index = port == Port::Two ? 1 : 0;
    return m_impl->controllers[index];
}

void Machine::setVideoSink(IVideoSink* sink) { m_impl->videoSink = sink; }
void Machine::setAudioSink(IAudioSink* sink)
{
    m_impl->apu->setAudioSink(sink);
}

CpuState Machine::cpu() const
{
    CpuState state = m_impl->cpu->state();
    state.masterCycle = m_impl->scheduler.now();
    return state;
}

void Machine::readMemory(std::uint16_t addr, std::size_t len,
    std::uint8_t* dst) const
{
    for (std::size_t i = 0; i < len; ++i) {
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
    return size == 0 ? LoadStateError::BadMagic
                     : LoadStateError::IncompatibleFormat;
}

SramRange Machine::sram() const
{
    const auto& ram = m_impl->cart.prgRam;
    if (ram.empty()) {
        return SramRange{};
    }
    return SramRange{ram.data(), ram.size()};
}

void Machine::loadSram(const std::uint8_t* data, std::size_t size)
{
    auto& ram = m_impl->cart.prgRam;
    if (ram.empty() || data == nullptr) {
        return;
    }
    const std::size_t n = size < ram.size() ? size : ram.size();
    std::memcpy(ram.data(), data, n);
}

void Machine::setInstructionTrace(
    std::function<void(const TracePoint&)> callback)
{
    auto& impl = *m_impl;
    Ppu* ppu = impl.ppu.get();
    impl.cpu->setInstructionTrace(
        [ppu, cb = std::move(callback)](const CpuState& cpu) {
            cb(TracePoint{cpu, ppu->scanline(), ppu->dot()});
        });
}

void Machine::debugSetProgramCounter(std::uint16_t pc)
{
    m_impl->cpu->setProgramCounter(pc);
}

std::uint8_t Machine::debugPeek(std::uint16_t addr) const
{
    return m_impl->bus.peek(addr);
}

Region Machine::region() const { return m_impl->regionId; }

std::uint64_t Machine::frameCounter() const { return m_impl->frameCounter; }

std::uint64_t Machine::masterCycle() const { return m_impl->scheduler.now(); }

} // namespace cutenes
