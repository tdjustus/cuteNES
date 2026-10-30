// Machine facade — wires CPU, PPU, APU, cartridge, and the run loop
// (tasks T010/T011 skeleton superseded by this integration; tasks
// T019-T026 components).

#include "cutenes/system/machine_impl.hpp"

#include "cutenes/cpu/trace.hpp"

#include "cutenes/serialize/save_state.hpp"

#include <cstring>
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
    cpu->setIrqLineSource([this] {
        return apu->frameIrqLine() || apu->dmcIrqPending()
            || cart.irqLine();
    });

    ppu->setVideoOutput(framebuffer.data());
    apu->setOutputRate(config.audioSampleRate);
    if (audioSink != nullptr) {
        apu->setAudioSink(audioSink);
    }

    io = ApuIoBridge{};
    io.sched = &scheduler;
    io.bus = &bus;
    io.controllers = controllers;
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
        cpu->setIrq(apu->frameIrqLine() || apu->dmcIrqPending()
            || cart.irqLine());
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
        // Rebuild the PPU/APU/CPU for the new timing profile in place (they
        // are recreated, so no stale references survive). Sinks are kept.
        impl.buildComponents(region);
    }
    m_impl->ppu->attachCartridge(&m_impl->cart);
    m_impl->cart.attachScheduler(m_impl->scheduler);
    applyMmc3Revision(); // header default or the stored manual override
    m_impl->runState = RunState::Paused;
    return result;
}

void Machine::setMmc3Revision(Mmc3Revision revision)
{
    auto& impl = *m_impl;
    impl.mmc3RevSel = static_cast<int>(revision);
    if (impl.cart.mapper != nullptr) {
        applyMmc3Revision();
    }
}

Machine::Mmc3Revision Machine::mmc3Revision() const
{
    return static_cast<Mmc3Revision>(m_impl->mmc3RevSel);
}

// Resolve Auto from the header (NES 2.0 mapper 4 submapper 4 = revision A)
// and forward the effective choice to the mapper.
void Machine::applyMmc3Revision()
{
    auto& impl = *m_impl;
    if (impl.cart.info.mapperId != 4) {
        return; // not an MMC3 cartridge
    }
    const bool revA = impl.mmc3RevSel == 2
        || (impl.mmc3RevSel == 0 && impl.cart.info.submapper == 4);
    impl.cart.mapper->setRevisionA(revA);
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
    // Reset order matters for the APU frame counter: the $4017 rewrite
    // must be stamped at the RESET INSTANT, before the CPU's 8-cycle reset
    // sequence advances the clock (blargg apu_reset/4017_written pins the
    // sequencer restart to within a cycle of the reset assertion; the
    // powerOn order below is the same for the same reason).
    m_impl->ppu->reset(/*cold=*/false);
    m_impl->apu->reset(/*cold=*/false);
    m_impl->cpu->reset(/*cold=*/false);
}

void Machine::runFrames(std::uint32_t frames)
{
    for (std::uint32_t i = 0; i < frames; ++i) {
        m_impl->runUntilFrameComplete();
    }
}

void Machine::frameAdvance() { runFrames(1); }

void Machine::runFrames(std::uint32_t frames, const InputScript* script)
{
    auto& impl = *m_impl;
    for (std::uint32_t i = 0; i < frames; ++i) {
        if (script != nullptr && script->count > 0) {
            const std::size_t idx = i < script->count ? i
                                                      : script->count - 1;
            impl.controllers[0] = script->frames[idx]; // hold last
        }
        impl.runUntilFrameComplete();
    }
}

void Machine::writeCpu(const CpuWrite& w)
{
    m_impl->cpu->writeRegisters(w.mask, w.a, w.x, w.y, w.s, w.p, w.pc);
}

void Machine::readPpuRegisters(std::uint8_t dst[8]) const
{
    m_impl->ppu->debugReadRegisters(dst);
}

void Machine::readVram(std::uint16_t addr, std::size_t len,
    std::uint8_t* dst) const
{
    m_impl->ppu->debugReadVram(addr, len, dst);
}

void Machine::writeVram(std::uint16_t addr, const std::uint8_t* src,
    std::size_t len)
{
    m_impl->ppu->debugWriteVram(addr, src, len);
}

void Machine::readOam(std::uint8_t dst[256]) const
{
    m_impl->ppu->debugReadOam(dst);
}

void Machine::writeOam(const std::uint8_t src[256])
{
    m_impl->ppu->debugWriteOam(src);
}

void Machine::readPalette(std::uint8_t dst[32]) const
{
    m_impl->ppu->debugReadPalette(dst);
}

void Machine::writePalette(const std::uint8_t src[32])
{
    m_impl->ppu->debugWritePalette(src);
}

namespace {
const Machine* g_peekTarget = nullptr;
std::uint8_t peekThunk(std::uint16_t addr)
{
    return g_peekTarget != nullptr
        ? const_cast<Machine*>(g_peekTarget)->peek(addr)
        : 0;
}
} // namespace

std::uint8_t Machine::peek(std::uint16_t addr) const
{
    return m_impl->bus.peek(addr);
}

std::string Machine::disassemble(std::uint16_t pc) const
{
    auto& impl = *m_impl;
    CpuState state = impl.cpu->state();
    state.pc = pc;
    const TracePoint tp{state, impl.ppu->scanline(), impl.ppu->dot()};
    g_peekTarget = this; // single-threaded core; set for the call
    std::string line = traceLine(tp, &peekThunk);
    g_peekTarget = nullptr;
    return line;
}

void Machine::stepInstruction()
{
    auto& impl = *m_impl;
    impl.cpu->setIrq(impl.apu->frameIrqLine()
        || impl.apu->dmcIrqPending()
        || impl.cart.irqLine());
    impl.cpu->step();
    impl.ppu->runTo(impl.scheduler.now());
    impl.apu->runTo(impl.scheduler.now());
    // Frame delivery must not depend on which API surface advanced time.
    if (impl.ppu->takeFrameReady()) {
        impl.apu->flushToSink();
        impl.deliverFrame();
        ++impl.frameCounter;
    }
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
    m_impl->audioSink = sink;
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

std::uint16_t Machine::Impl::cartridgeId() const
{
    // FNV-1a of (mapperId, prgSize, chrSize, mirroring) — the contract's
    // WrongCartridge discriminator.
    std::uint32_t h = 0x811C9DC5u;
    const auto mix = [&h](std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            h ^= (v >> (i * 8)) & 0xFF;
            h *= 0x01000193u;
        }
    };
    mix(cart.info.mapperId);
    mix(static_cast<std::uint32_t>(cart.info.prgSize));
    mix(static_cast<std::uint32_t>(cart.info.chrSize));
    mix(static_cast<std::uint32_t>(cart.info.mirroring));
    return static_cast<std::uint16_t>((h >> 16) ^ h);
}

SaveStateBlob Machine::Impl::capture()
{
    stateBlob.clear();
    StateWriter w(stateBlob);
    cpu->saveState(w);
    ppu->saveState(w);
    apu->saveState(w);
    bus.saveState(w);
    if (cart.mapper) {
        cart.mapper->saveState(w);
    }
    // INP: controller shift state.
    w.begin("INP ");
    w.w(io.strobe);
    w.w(io.shiftPos[0]);
    w.w(io.shiftPos[1]);
    w.w<std::uint64_t>(controllers[0].to_ulong());
    w.w<std::uint64_t>(controllers[1].to_ulong());
    w.end();
    // SCH: the sole time source.
    w.begin("SCH ");
    w.w<std::uint64_t>(scheduler.now());
    w.w<std::uint64_t>(scheduler.frameIndex());
    w.w<std::uint64_t>(frameCounter);
    w.end();
    // SRAM (flag bit 0).
    if (!cart.prgRam.empty() && cart.info.battery) {
        w.begin("SRAM");
        w.wBytes(cart.prgRam.data(), cart.prgRam.size());
        w.end();
    }
    const std::uint16_t flags =
        cart.info.battery && !cart.prgRam.empty() ? 1 : 0;
    w.finish(1, flags, coreVersion,
        regionId == Region::Pal ? 1 : 0, cartridgeId());
    return SaveStateBlob{stateBlob.data(), stateBlob.size()};
}

LoadStateError Machine::Impl::applyState(const std::uint8_t* data,
    std::size_t size)
{
    if (size < StateReader::kHeaderSize
        || std::memcmp(data, "CTNS", 4) != 0) {
        return LoadStateError::BadMagic;
    }
    StateReader r(data, size);
    if (r.formatVersion() != 1) {
        return LoadStateError::IncompatibleFormat;
    }
    if (r.coreVersion() != coreVersion) {
        return LoadStateError::IncompatibleCoreVersion;
    }
    if (r.cartridgeId() != cartridgeId()) {
        return LoadStateError::WrongCartridge;
    }
    if (StateWriter::crc32(data + StateReader::kHeaderSize,
            r.sectionsSize())
        != r.checksum()) {
        return LoadStateError::ChecksumMismatch;
    }
    // All header validation passed; sections parse structurally.
    if (!r.seek("CPU ") || !r.seek("PPU ") || !r.seek("APU ")
        || !r.seek("RAM ") || !r.seek("SCH ")) {
        return LoadStateError::IncompatibleFormat;
    }
    // Apply.
    r.seek("CPU ");
    cpu->loadState(r);
    r.seek("PPU ");
    ppu->loadState(r);
    r.seek("APU ");
    apu->loadState(r);
    r.seek("RAM ");
    bus.loadState(r);
    if (cart.mapper && r.seek("MAP ")) {
        cart.mapper->loadState(r);
    }
    if (r.seek("INP ")) {
        io.strobe = r.get<std::uint8_t>();
        io.shiftPos[0] = r.get<std::uint8_t>();
        io.shiftPos[1] = r.get<std::uint8_t>();
        controllers[0] = ButtonSet{r.get<std::uint64_t>()};
        controllers[1] = ButtonSet{r.get<std::uint64_t>()};
    }
    r.seek("SCH ");
    const std::uint64_t master = r.get<std::uint64_t>();
    const std::uint64_t frame = r.get<std::uint64_t>();
    frameCounter = r.get<std::uint64_t>();
    scheduler.forceTo(master, frame);
    if (r.flags() & 1) {
        r.seek("SRAM");
        r.getBytes(cart.prgRam.data(), cart.prgRam.size());
    }
    runState = cart.info.prgSize > 0 ? RunState::Paused : RunState::NeedsRom;
    return LoadStateError::Ok;
}

SaveStateBlob Machine::captureState()
{
    return m_impl->capture();
}

LoadStateError Machine::loadState(const std::uint8_t* data, std::size_t size)
{
    return m_impl->applyState(data, size);
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

Region Machine::region() const { return m_impl->regionId; }

std::uint64_t Machine::frameCounter() const { return m_impl->frameCounter; }

std::uint64_t Machine::masterCycle() const { return m_impl->scheduler.now(); }

} // namespace cutenes

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace cutenes {

namespace {

void putLe(std::vector<std::uint8_t>& out, std::uint64_t value, int bytes)
{
    for (int i = 0; i < bytes; ++i) {
        out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
    }
}

} // namespace

std::vector<std::uint8_t> Machine::memoryDump(std::uint64_t timestamp) const
{
    // Layout: memdump-design.md / contracts/core-api.md "Memory dump".
    // Integers are little-endian so a dump reads the same on every host.
    std::vector<std::uint8_t> out;
    const auto& prgRam = m_impl->cart.prgRam;
    out.reserve(32 + 0x10000 + 8 + 256 + 32 + 4 + prgRam.size() + 8 + 0x4000);

    static constexpr char kMagic[8] = {'C', 'U', 'N', 'E', 'D', 'M', 'P', '1'};
    out.insert(out.end(), kMagic, kMagic + 8);
    putLe(out, timestamp, 8);
    putLe(out, frameCounter(), 8);
    putLe(out, masterCycle(), 8);

    for (std::uint32_t addr = 0x0000; addr < 0x10000; ++addr) {
        out.push_back(peek(static_cast<std::uint16_t>(addr)));
    }

    std::uint8_t regs[8]{};
    readPpuRegisters(regs);
    out.insert(out.end(), regs, regs + 8);
    std::uint8_t oam[256]{};
    readOam(oam);
    out.insert(out.end(), oam, oam + 256);
    std::uint8_t palette[32]{};
    readPalette(palette);
    out.insert(out.end(), palette, palette + 32);

    putLe(out, prgRam.size(), 4);
    out.insert(out.end(), prgRam.begin(), prgRam.end());

    // The whole PPU bus through the debug view: pattern tables (CHR ROM or
    // RAM), nametables with mirroring resolved, and the palette.
    static constexpr char kVram[4] = {'V', 'R', 'A', 'M'};
    out.insert(out.end(), kVram, kVram + 4);
    constexpr std::size_t kPpuBus = 0x4000;
    putLe(out, kPpuBus, 4);
    const std::size_t at = out.size();
    out.resize(at + kPpuBus);
    readVram(0x0000, kPpuBus, out.data() + at);
    return out;
}

std::string Machine::dumpMemory(const std::string& debugDir) const
{
    namespace fs = std::filesystem;
    fs::create_directories(debugDir);

    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
#if defined(_WIN32)
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif
    std::stringstream ss;
    ss << std::put_time(&tm, "%Y%m%d%H%M%S");
    const fs::path outPath = fs::path(debugDir) / (ss.str() + ".dmp");

    const std::vector<std::uint8_t> bytes =
        memoryDump(static_cast<std::uint64_t>(time));
    std::ofstream out(outPath, std::ios::binary);
    if (out) {
        out.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    }
    return outPath.string();
}

} // namespace cutenes
