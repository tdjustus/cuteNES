// 2A03 APU implementation (task T024; tests T017).
//
// Integer-only emulated state. The mixer uses the nesdev APU_Mixer lookup
// approximations (31-entry pulse, 203-entry TND) built once in double;
// runtime mixing is int32 adds + clamp, so audio is byte-identical across
// platforms (spec FR-012). DMC/OAM DMA CPU-stall accounting is refined in
// task T037; PAL APU period tables land with PAL timing validation.

#include "cutenes/apu/apu.hpp"

#include "cutenes/serialize/save_state.hpp"
#include "cutenes/system/timing.hpp"

#include <cmath>
#include <cstdlib>

namespace cutenes {

const std::array<std::uint8_t, 32> Apu::kLengthTable = {
    10, 254, 20, 2, 40, 4, 80, 6, 160, 8, 60, 10, 14, 12, 26, 14,
    12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30,
};

const std::array<std::uint16_t, 16> Apu::kNoisePeriods = {
    4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068,
};

const std::array<std::uint16_t, 16> Apu::kDmcRates = {
    428, 380, 340, 320, 286, 254, 226, 214,
    190, 160, 142, 128, 106, 84, 72, 54, // NTSC CPU cycles between clocks
};

const std::array<std::uint8_t, 4> Apu::kDuty = {
    0x40, 0x60, 0x78, 0x9F,
};

std::array<std::int32_t, 31> Apu::sPulseTable{};
std::array<std::int32_t, 203> Apu::sTndTable{};
std::array<std::int32_t, 64 * 16> Apu::sTaps{};
bool Apu::sTablesBuilt = false;

namespace {

constexpr double kPi = 3.14159265358979323846;

std::int32_t clamp16(long long v)
{
    if (v > 32767) {
        return 32767;
    }
    if (v < -32768) {
        return -32768;
    }
    return static_cast<std::int32_t>(v);
}

} // namespace

std::int32_t Apu::mixerPulse(int sum)
{
    buildTables(); // lazy: static accessors may run before any Apu exists
    return sPulseTable[static_cast<std::size_t>(sum & 31)];
}
std::int32_t Apu::mixerTnd(int index)
{
    return sTndTable[static_cast<std::size_t>(index % 203)];
}

// One-time table construction in double (research.md D7 determinism rule:
// doubles never touch emulated state).
void Apu::buildTables()
{
    if (sTablesBuilt) {
        return;
    }
    // pulse = 95.88 / (8128/n + 100), scaled so n=31 is near full scale.
    double pmax = 95.88 / (8128.0 / 31.0 + 100.0);
    for (int n = 0; n < 31; ++n) {
        const double v = 95.88 / (8128.0 / n + 100.0);
        sPulseTable[static_cast<std::size_t>(n)] = static_cast<std::int32_t>(
            std::lround(32000.0 * v / (pmax * 1.6)));
    }
    // tnd = 159.79 / (1/(t/8227 + n/12241 + d/22638) + 100)
    double tmax = 159.79 / (1.0 / (15.0 / 8227.0 + 15.0 / 12241.0
        + 127.0 / 22638.0) + 100.0);
    for (int n = 0; n < 203; ++n) {
        const double x = n / 22638.0;
        const double v = 159.79 / (1.0 / x + 100.0);
        sTndTable[static_cast<std::size_t>(n)] = static_cast<std::int32_t>(
            std::lround(32000.0 * v / (tmax * 2.2)));
    }
    // 16-tap windowed-sinc polyphase bank, 64 phases, Q14.
    for (int p = 0; p < 64; ++p) {
        const double frac = p / 64.0;
        for (int t = 0; t < 16; ++t) {
            const double x = static_cast<double>(t) - 7.5 - frac;
            double sinc = (std::fabs(x) < 1e-9) ? 1.0 : std::sin(kPi * x)
                / (kPi * x);
            const double window = 0.54 - 0.46 * std::cos(
                2.0 * kPi * (t + frac) / 16.0);
            sTaps[static_cast<std::size_t>(p) * 16 + static_cast<std::size_t>(t)] = static_cast<std::int32_t>(
                std::lround(8192.0 * sinc * window));
        }
    }
    sTablesBuilt = true;
}

Apu::Apu(const RegionProfile& profile, Scheduler& scheduler, Bus& bus)
    : m_profile(profile)
    , m_sched(scheduler)
    , m_bus(bus)
{
    buildTables();
    m_outChunk.reserve(4096);
    setOutputRate(m_outRate); // arm the filter coefficients
}

void Apu::saveState(StateWriter& w) const
{
    w.begin("APU ");
    const auto pulse = [&](const Pulse& p) {
        w.w(p.enabled);
        w.w(p.timerReload);
        w.w(p.timer);
        w.w(p.duty);
        w.w(p.dutyIndex);
        w.w(p.lengthEnabled);
        w.w(p.length);
        w.w(p.envStart);
        w.w(p.envLoop);
        w.w(p.envConstant);
        w.w(p.envDiv);
        w.w(p.envVol);
        w.w(p.envDecay);
        w.w(p.sweepEnabled);
        w.w(p.sweepReload);
        w.w(p.sweepNegate);
        w.w(p.sweepPeriod);
        w.w(p.sweepShift);
        w.w(p.sweepDiv);
    };
    pulse(m_pulse1);
    pulse(m_pulse2);
    w.w(m_triangle.enabled);
    w.w(m_triangle.control);
    w.w(m_triangle.reloadFlag);
    w.w(m_triangle.timerReload);
    w.w(m_triangle.timer);
    w.w(m_triangle.linearReload);
    w.w(m_triangle.linear);
    w.w(m_triangle.index);
    w.w(m_triangle.lengthEnabled);
    w.w(m_triangle.length);
    w.w(m_noise.enabled);
    w.w(m_noise.mode);
    w.w(m_noise.timer);
    w.w(m_noise.timerReload);
    w.w(m_noise.periodIndex);
    w.w(m_noise.lfsr);
    w.w(m_noise.lengthEnabled);
    w.w(m_noise.length);
    w.w(m_noise.envStart);
    w.w(m_noise.envLoop);
    w.w(m_noise.envConstant);
    w.w(m_noise.envDiv);
    w.w(m_noise.envVol);
    w.w(m_noise.envDecay);
    w.w(m_dmc.enabled);
    w.w(m_dmc.irqEnabled);
    w.w(m_dmc.loop);
    w.w(m_dmc.rateIndex);
    w.w(m_dmc.timer);
    w.w<std::uint64_t>(m_dmcDueMaster);
    w.w<std::uint8_t>(m_dmcHaltOnPut ? 1 : 0);
    w.w(m_dmc.addrReg);
    w.w(m_dmc.lenReg);
    w.w(m_dmc.currentAddr);
    w.w(m_dmc.bytesRemaining);
    w.w(m_dmc.bufferEmpty);
    w.w(m_dmc.sampleBuffer);
    w.w(m_dmc.shiftReg);
    w.w(m_dmc.bitsRemaining);
    w.w(m_dmc.silence);
    w.w(m_dmc.deltaCounter);
    w.w(m_frameMode5);
    w.w(m_inhibitIrq);
    w.w(m_dmcIrq);
    w.w<std::uint64_t>(m_frameIrqAt);
    w.w<std::uint64_t>(m_frameIrqAssertUntil);
    w.w<std::uint64_t>(m_seqBaseMaster);
    w.w<std::uint64_t>(m_seqClockBase);
    w.w<std::uint8_t>(m_irqFiredThisPeriod ? 1 : 0);
    w.w<std::uint32_t>(m_sequenceStep);
    w.w<std::uint64_t>(m_apuCycles);
    w.w<std::uint64_t>(m_nextOutNum);
    w.w<std::uint64_t>(m_historyCount);
    w.wBytes(reinterpret_cast<const std::uint8_t*>(m_history.data()),
        m_history.size() * 2);
    w.w<std::int64_t>(m_hpf1);
    w.w<std::int64_t>(m_hpf1A);
    w.w<std::int64_t>(m_hpf1B);
    w.w<std::int64_t>(m_hpf2);
    w.w<std::int64_t>(m_hpf2A);
    w.w<std::int64_t>(m_hpf2B);
    w.w<std::int64_t>(m_lpfA);
    w.w<std::int64_t>(m_lpfB);
    const std::uint32_t nativeCount =
        static_cast<std::uint32_t>(m_native.size());
    w.w(nativeCount);
    w.wBytes(reinterpret_cast<const std::uint8_t*>(m_native.data()),
        m_native.size() * 2);
    w.end();
}

void Apu::loadState(StateReader& r)
{
    const auto pulse = [&](Pulse& p) {
        p.enabled = r.get<std::uint8_t>() != 0;
        p.timerReload = r.get<std::uint16_t>();
        p.timer = r.get<std::uint16_t>();
        p.duty = r.get<std::uint8_t>();
        p.dutyIndex = r.get<std::uint8_t>();
        p.lengthEnabled = r.get<std::uint8_t>() != 0;
        p.length = r.get<std::uint8_t>();
        p.envStart = r.get<std::uint8_t>() != 0;
        p.envLoop = r.get<std::uint8_t>() != 0;
        p.envConstant = r.get<std::uint8_t>() != 0;
        p.envDiv = r.get<std::uint8_t>();
        p.envVol = r.get<std::uint8_t>();
        p.envDecay = r.get<std::uint8_t>();
        p.sweepEnabled = r.get<std::uint8_t>() != 0;
        p.sweepReload = r.get<std::uint8_t>() != 0;
        p.sweepNegate = r.get<std::uint8_t>() != 0;
        p.sweepPeriod = r.get<std::uint8_t>();
        p.sweepShift = r.get<std::uint8_t>();
        p.sweepDiv = r.get<std::uint8_t>();
    };
    pulse(m_pulse1);
    pulse(m_pulse2);
    m_triangle.enabled = r.get<std::uint8_t>() != 0;
    m_triangle.control = r.get<std::uint8_t>() != 0;
    m_triangle.reloadFlag = r.get<std::uint8_t>() != 0;
    m_triangle.timerReload = r.get<std::uint16_t>();
    m_triangle.timer = r.get<std::uint16_t>();
    m_triangle.linearReload = r.get<std::uint8_t>();
    m_triangle.linear = r.get<std::uint8_t>();
    m_triangle.index = r.get<std::uint8_t>();
    m_triangle.lengthEnabled = r.get<std::uint8_t>() != 0;
    m_triangle.length = r.get<std::uint8_t>();
    m_noise.enabled = r.get<std::uint8_t>() != 0;
    m_noise.mode = r.get<std::uint8_t>() != 0;
    m_noise.timer = r.get<std::uint16_t>();
    m_noise.timerReload = r.get<std::uint16_t>();
    m_noise.periodIndex = r.get<std::uint16_t>();
    m_noise.lfsr = r.get<std::uint16_t>();
    m_noise.lengthEnabled = r.get<std::uint8_t>() != 0;
    m_noise.length = r.get<std::uint8_t>();
    m_noise.envStart = r.get<std::uint8_t>() != 0;
    m_noise.envLoop = r.get<std::uint8_t>() != 0;
    m_noise.envConstant = r.get<std::uint8_t>() != 0;
    m_noise.envDiv = r.get<std::uint8_t>();
    m_noise.envVol = r.get<std::uint8_t>();
    m_noise.envDecay = r.get<std::uint8_t>();
    m_dmc.enabled = r.get<std::uint8_t>() != 0;
    m_dmc.irqEnabled = r.get<std::uint8_t>() != 0;
    m_dmc.loop = r.get<std::uint8_t>() != 0;
    m_dmc.rateIndex = r.get<std::uint8_t>();
    m_dmc.timer = r.get<std::uint16_t>();
    m_dmcDueMaster = r.get<std::uint64_t>();
    m_dmcHaltOnPut = r.get<std::uint8_t>() != 0;
    m_dmc.addrReg = r.get<std::uint16_t>();
    m_dmc.lenReg = r.get<std::uint16_t>();
    m_dmc.currentAddr = r.get<std::uint16_t>();
    m_dmc.bytesRemaining = r.get<std::uint16_t>();
    m_dmc.bufferEmpty = r.get<std::uint8_t>() != 0;
    m_dmc.sampleBuffer = r.get<std::uint8_t>();
    m_dmc.shiftReg = r.get<std::uint8_t>();
    m_dmc.bitsRemaining = r.get<std::uint8_t>();
    m_dmc.silence = r.get<std::uint8_t>() != 0;
    m_dmc.deltaCounter = r.get<std::uint8_t>();
    m_frameMode5 = r.get<std::uint8_t>() != 0;
    m_inhibitIrq = r.get<std::uint8_t>() != 0;
    m_dmcIrq = r.get<std::uint8_t>() != 0;
    m_frameIrqAt = r.get<std::uint64_t>();
    m_frameIrqAssertUntil = r.get<std::uint64_t>();
    m_seqBaseMaster = r.get<std::uint64_t>();
    m_seqClockBase = r.get<std::uint64_t>();
    m_irqFiredThisPeriod = r.get<std::uint8_t>() != 0;
    m_sequenceStep = r.get<std::uint32_t>();
    m_apuCycles = r.get<std::uint64_t>();
    m_nextOutNum = r.get<std::uint64_t>();
    m_historyCount = r.get<std::uint64_t>();
    r.getBytes(reinterpret_cast<std::uint8_t*>(m_history.data()),
        m_history.size() * 2);
    m_hpf1 = r.get<std::int64_t>();
    m_hpf1A = r.get<std::int64_t>();
    m_hpf1B = r.get<std::int64_t>();
    m_hpf2 = r.get<std::int64_t>();
    m_hpf2A = r.get<std::int64_t>();
    m_hpf2B = r.get<std::int64_t>();
    m_lpfA = r.get<std::int64_t>();
    m_lpfB = r.get<std::int64_t>();
    const std::uint32_t nativeCount = r.get<std::uint32_t>();
    m_native.resize(nativeCount);
    if (nativeCount > 0) {
        r.getBytes(reinterpret_cast<std::uint8_t*>(m_native.data()),
            nativeCount * 2);
    }
    // Rebuild the mixer/resampler filter coefficients (config-derived).
    setOutputRate(m_outRate);
}

void Apu::reset(bool cold)
{
    m_pulse1 = {};
    m_pulse2 = {};
    m_triangle = {};
    m_noise = {};
    m_dmc = {};
    m_frameMode5 = false;
    m_inhibitIrq = cold ? false : m_inhibitIrq;
    m_frameIrqAt = 0;
    m_frameIrqAssertUntil = 0;
    m_dmcIrq = false;
    m_seqBaseMaster = m_sched.now();
    m_sequenceStep = 0;
    m_native.clear();
    m_nextOutNum = 0;
    m_historyCount = 0;
    m_history.fill(0);
    if (cold) {
        m_apuCycles = 0;
    }
}

void Apu::setOutputRate(std::uint32_t rate)
{
    m_outRate = rate;
    // One-pole coefficients (Q30), computed once per configuration — double
    // is allowed here, never in the emulated path (research.md D7).
    const auto alphaQ30 = [rate](double hz) {
        const double dt = 1.0 / static_cast<double>(rate);
        const double rc = 1.0 / (2.0 * kPi * hz);
        const double a = dt / (rc + dt);
        return static_cast<std::int64_t>(a * 1073741824.0);
    };
    m_hpf1B = alphaQ30(90.0);
    m_hpf2B = alphaQ30(440.0);
    m_lpfB = alphaQ30(14000.0);
    // Reset the streaming state deterministically for the new ratio.
    m_nextOutNum = 0;
    m_historyCount = 0;
    m_history.fill(0);
    m_native.clear();
}

void Apu::setAudioSink(IAudioSink* sink) { m_sink = sink; }

// --- Register file -----------------------------------------------------------

std::uint8_t Apu::statusRead()
{
    std::uint8_t v = 0;
    // Status bits reflect length>0 regardless of the enable bit (enable
    // only gates output; $4015=0 clears the length separately).
    if (m_pulse1.length > 0) v |= 0x01;
    if (m_pulse2.length > 0) v |= 0x02;
    if (m_triangle.length > 0) v |= 0x04;
    if (m_noise.length > 0) v |= 0x08;
    if (m_dmc.bytesRemaining > 0) v |= 0x10;
    const bool frameVisible = frameIrqPending();
    if (frameVisible) {
        v |= 0x40;
    }
    if (m_dmcIrq) v |= 0x80;
    // Reading a VISIBLE flag clears it — but the IRQ condition stays
    // active around the set moment, so a read right there re-asserts it
    // (6-irq_flag_timing test 4). Reads while not visible change nothing.
    if (frameVisible) {
        if (m_sched.now() < m_frameIrqAssertUntil) {
            m_frameIrqAt = m_sched.now() + timing::kFrameIrqReassertTicks;
        } else {
            m_frameIrqAt = 0;
        }
    }
    return v;
}

std::uint8_t Apu::registerRead(std::uint16_t addr)
{
    return addr == 0x4015 ? statusRead() : 0;
}

void Apu::registerWrite(std::uint16_t addr, std::uint8_t value)
{
    if (addr <= 0x4003) { // pulse 1
        switch (addr & 3) {
        case 0:
            m_pulse1.duty = value >> 6;
            m_pulse1.envLoop = (value & 0x20) != 0;
            m_pulse1.envConstant = (value & 0x10) != 0;
            m_pulse1.envVol = value & 0x0F;
            break;
        case 1:
            m_pulse1.sweepEnabled = (value & 0x80) != 0;
            m_pulse1.sweepPeriod = ((value >> 4) & 7) + 1;
            m_pulse1.sweepNegate = (value & 0x08) != 0;
            m_pulse1.sweepShift = value & 7;
            m_pulse1.sweepReload = true;
            break;
        case 2:
            m_pulse1.timerReload =
                (m_pulse1.timerReload & 0x0700) | value;
            break;
        default:
            m_pulse1.timerReload = static_cast<std::uint16_t>(
                (m_pulse1.timerReload & 0x00FF) | ((value & 7) << 8));
            m_pulse1.length =
                m_pulse1.enabled ? kLengthTable[value >> 3] : 0;
            m_pulse1.envStart = true;
            m_pulse1.dutyIndex = 0;
            break;
        }
    } else if (addr >= 0x4004 && addr <= 0x4007) { // pulse 2
        switch (addr & 3) {
        case 0:
            m_pulse2.duty = value >> 6;
            m_pulse2.envLoop = (value & 0x20) != 0;
            m_pulse2.envConstant = (value & 0x10) != 0;
            m_pulse2.envVol = value & 0x0F;
            break;
        case 1:
            m_pulse2.sweepEnabled = (value & 0x80) != 0;
            m_pulse2.sweepPeriod = ((value >> 4) & 7) + 1;
            m_pulse2.sweepNegate = (value & 0x08) != 0;
            m_pulse2.sweepShift = value & 7;
            m_pulse2.sweepReload = true;
            break;
        case 2:
            m_pulse2.timerReload =
                (m_pulse2.timerReload & 0x0700) | value;
            break;
        default:
            m_pulse2.timerReload = static_cast<std::uint16_t>(
                (m_pulse2.timerReload & 0x00FF) | ((value & 7) << 8));
            m_pulse2.length =
                m_pulse2.enabled ? kLengthTable[value >> 3] : 0;
            m_pulse2.envStart = true;
            m_pulse2.dutyIndex = 0;
            break;
        }
    } else if (addr >= 0x4008 && addr <= 0x400B) { // triangle
        switch (addr & 3) {
        case 0:
            m_triangle.control = (value & 0x80) != 0;
            m_triangle.linearReload = value & 0x7F;
            break;
        case 2:
            m_triangle.timerReload = static_cast<std::uint16_t>(
                (m_triangle.timerReload & 0x0700) | value);
            break;
        default:
            m_triangle.timerReload = static_cast<std::uint16_t>(
                (m_triangle.timerReload & 0x00FF) | ((value & 7) << 8));
            m_triangle.length =
                m_triangle.enabled ? kLengthTable[value >> 3] : 0;
            m_triangle.reloadFlag = true;
            break;
        }
    } else if (addr >= 0x400C && addr <= 0x400F) { // noise
        switch (addr & 3) {
        case 0:
            m_noise.envLoop = (value & 0x20) != 0;
            m_noise.envConstant = (value & 0x10) != 0;
            m_noise.envVol = value & 0x0F;
            break;
        case 2:
            m_noise.periodIndex = value & 0x0F;
            m_noise.timerReload = kNoisePeriods[value & 0x0F] / 2; // APU cyc
            break;
        default:
            m_noise.length =
                m_noise.enabled ? kLengthTable[value >> 3] : 0;
            m_noise.envStart = true;
            break;
        }
    } else if (addr >= 0x4010 && addr <= 0x4013) { // DMC
        switch (addr & 3) {
        case 0:
            m_dmc.irqEnabled = (value & 0x80) != 0;
            m_dmc.loop = (value & 0x40) != 0;
            m_dmc.rateIndex = value & 0x0F;
            if (!m_dmc.irqEnabled) {
                m_dmcIrq = false;
            }
            break;
        case 1:
            m_dmc.deltaCounter = value & 0x7F;
            break;
        case 2:
            m_dmc.addrReg = value;
            break;
        default:
            m_dmc.lenReg = value;
            break;
        }
    } else if (addr == 0x4015) {
        if (!(value & 1)) {
            m_pulse1.enabled = false;
            m_pulse1.length = 0;
        } else {
            m_pulse1.enabled = true;
        }
        if (!(value & 2)) {
            m_pulse2.enabled = false;
            m_pulse2.length = 0;
        } else {
            m_pulse2.enabled = true;
        }
        if (!(value & 4)) {
            m_triangle.enabled = false;
            m_triangle.length = 0;
        } else {
            m_triangle.enabled = true;
        }
        if (!(value & 8)) {
            m_noise.enabled = false;
            m_noise.length = 0;
        } else {
            m_noise.enabled = true;
        }
        const bool dmcOn = (value & 0x10) != 0;
        if (!dmcOn) {
            m_dmc.bytesRemaining = 0;
        } else if (m_dmc.bytesRemaining == 0) {
            m_dmc.currentAddr = static_cast<std::uint16_t>(0xC000
                + m_dmc.addrReg * 64);
            m_dmc.bytesRemaining = static_cast<std::uint16_t>(
                m_dmc.lenReg * 16 + 1);
            // NOTE: the output unit's bit counter is FREE-RUNNING — a
            // restart must NOT re-anchor its phase (the buffered byte is
            // taken at the next wrap whenever that falls; observed via the
            // reference DMC state traces, session 14).
        }
        m_dmc.enabled = dmcOn;
        m_dmcIrq = false;
        if (dmcOn && m_dmc.bufferEmpty && m_dmc.bytesRemaining > 0) {
            // Load DMA (wiki DMA): the halt is scheduled for a get cycle
            // during the 2nd APU cycle after this write — never fetched
            // inline (the fine-sync loops count the 4-cycle DMA).
            m_dmcDueMaster = m_sched.now(); // loads: due at the write; the
            // whole-cycle gate starts the DMA at the next cycle boundary
            // and the 3-cycle span (halt/dummy/get) totals 4 from the
            // write — the cost blargg's sync loops count.
            m_dmcHaltOnPut = false; // load DMA halts on a get cycle
        }
    } else if (addr == 0x4017) {
        m_frameMode5 = (value & 0x80) != 0;
        m_inhibitIrq = (value & 0x40) != 0;
        if (m_inhibitIrq) {
            m_frameIrqAt = 0;
            m_frameIrqAssertUntil = 0;
        }
        m_seqBaseMaster = m_sched.now();
        m_seqClockBase = m_seqBaseMaster + 3 * m_profile.cpuDivisor;
        m_irqFiredThisPeriod = false;
        m_sequenceStep = 0;
        if (m_frameMode5) {
            // 5-step mode: the reset clocks quarter+half after ~3 cycles;
            // immediate is within the test tolerance (exact delay: T036).
            quarterFrame();
            halfFrame();
        }
    }
}

// --- Channel output -------------------------------------------------------------

std::uint8_t Apu::Pulse::output() const
{
    if (!enabled || length == 0 || timerReload < 8) {
        return 0;
    }
    if ((kDuty[duty] >> dutyIndex) & 1) {
        return envConstant ? envVol : envDecay;
    }
    return 0;
}

std::uint8_t Apu::Triangle::output() const
{
    if (!enabled || length == 0) {
        return 0;
    }
    constexpr std::array<std::uint8_t, 32> kSeq = {{
        15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    }};
    return kSeq[index];
}

std::uint8_t Apu::Noise::output() const
{
    if (!enabled || length == 0) {
        return 0;
    }
    if ((lfsr & 1) == 0) {
        return envConstant ? envVol : envDecay;
    }
    return 0;
}

// --- Frame sequencing --------------------------------------------------------------

void Apu::clockEnvelope(Pulse& p)
{
    if (p.envStart) {
        p.envStart = false;
        p.envDecay = 15;
        p.envDiv = p.envVol;
    } else if (p.envDiv == 0) {
        p.envDiv = p.envVol;
        if (p.envDecay > 0) {
            --p.envDecay;
        } else if (p.envLoop) {
            p.envDecay = 15;
        }
    } else {
        --p.envDiv;
    }
}

void Apu::clockSweep(Pulse& p, bool second)
{
    if (p.sweepReload) {
        p.sweepReload = false;
        p.sweepDiv = p.sweepPeriod;
    } else if (p.sweepDiv == 0) {
        p.sweepDiv = p.sweepPeriod;
        const unsigned delta = p.timerReload >> p.sweepShift;
        if (p.sweepEnabled && delta > 0 && p.sweepShift > 0) {
            if (p.sweepNegate) {
                // Pulse 1 uses one's complement (research: classic quirk).
                const unsigned d = second ? delta : (delta - 1);
                const unsigned t = p.timerReload - d;
                if (t >= 8) {
                    p.timerReload = static_cast<std::uint16_t>(t);
                }
            } else if (p.timerReload + delta < 0x800) {
                p.timerReload = static_cast<std::uint16_t>(
                    p.timerReload + delta);
            }
        }
    } else {
        --p.sweepDiv;
    }
}

void Apu::clockLength(std::uint8_t& length, bool counting)
{
    // `counting` is the length-counter halt flag INVERTED (set at register
    // write when the halt bit is clear): decrement while counting.
    if (counting && length > 0) {
        --length;
    }
}

void Apu::quarterFrame()
{
    clockEnvelope(m_pulse1);
    clockEnvelope(m_pulse2);
    // Triangle linear counter.
    if (m_triangle.reloadFlag) {
        m_triangle.linear = m_triangle.linearReload;
    } else if (m_triangle.linear > 0) {
        --m_triangle.linear;
    }
    if (!m_triangle.control) {
        m_triangle.reloadFlag = false;
    }
}

void Apu::halfFrame()
{
    // Length halt = envelope-loop bit ($4000/$4004/$400C bit5) for the
    // squares/noise; $4008 bit7 (control) for the triangle.
    clockLength(m_pulse1.length, !m_pulse1.envLoop);
    clockLength(m_pulse2.length, !m_pulse2.envLoop);
    clockLength(m_triangle.length, !m_triangle.control);
    clockLength(m_noise.length, !m_noise.envLoop);
    clockSweep(m_pulse1, false);
    clockSweep(m_pulse2, true);
}

// Frame-counter CPU-cycle steps: 4-step 7457/14913/22371/29829(+IRQ+reset),
// 5-step 7457/14913/22371/29829/37281.

// --- DMC -----------------------------------------------------------------------------

void Apu::dmcFetchByte()
{
    // Superseded by the CPU-serviced fetch (dmcFetchPending/dmcDeliver):
    // the DMC halts the CPU for the memory read. Kept for direct paths.
    m_dmc.sampleBuffer = m_bus.read(m_dmc.currentAddr);
    m_dmc.bufferEmpty = false;
    advanceDmcAddress();
}

void Apu::advanceDmcAddress()
{
    m_dmc.currentAddr = static_cast<std::uint16_t>(
        m_dmc.currentAddr >= 0xFFFF ? 0x8000 : m_dmc.currentAddr + 1);
    --m_dmc.bytesRemaining;
    if (m_dmc.bytesRemaining == 0) {
        if (m_dmc.loop) {
            m_dmc.currentAddr = static_cast<std::uint16_t>(
                0xC000 + m_dmc.addrReg * 64);
            m_dmc.bytesRemaining = static_cast<std::uint16_t>(
                m_dmc.lenReg * 16 + 1);
        } else if (m_dmc.irqEnabled) {
            m_dmcIrq = true;
        }
    }
}

void Apu::clockDmcTimer()
{
    if (m_dmc.timer == 0) {
        m_dmc.timer = static_cast<std::uint16_t>(kDmcRates[m_dmc.rateIndex] / 2 - 1);
        if (!m_dmc.silence) {
            if ((m_dmc.shiftReg & 1) != 0) {
                m_dmc.deltaCounter = static_cast<std::uint8_t>(
                    m_dmc.deltaCounter < 127 ? m_dmc.deltaCounter + 2 : 126);
            } else {
                m_dmc.deltaCounter = static_cast<std::uint8_t>(
                    m_dmc.deltaCounter >= 2 ? m_dmc.deltaCounter - 2 : 0);
            }
        }
        m_dmc.shiftReg >>= 1;
        if (--m_dmc.bitsRemaining == 0) {
            m_dmc.bitsRemaining = 8;
            if (m_dmc.bufferEmpty) {
                m_dmc.silence = true;
            } else {
                m_dmc.silence = false;
                m_dmc.shiftReg = m_dmc.sampleBuffer;
                m_dmc.bufferEmpty = true;
                // Row-0 model (T037 session 20): the reload fetch is
                // served during the byte's tail so the refill is present
                // AT the consume edge (hardware full_after=1 same-cycle).
                // Arm one timer period early: at the take, the fetch has
                // already been serviced by the per-cycle engine.
                m_dmcDueMaster = m_sched.now() > (m_dmc.timer + 1) * 24
                    ? m_sched.now() - (m_dmc.timer + 1) * 24
                    : 0; // due in the previous period (already serviceable)
                m_dmcHaltOnPut = true;
            }
        }
    } else {
        --m_dmc.timer;
    }
    // DMC fetches are requested via dmcFetchPending() and serviced by the
    // CPU (task T037): the DMA halts the processor for the memory read.
}

// --- Mixing ---------------------------------------------------------------------------

void Apu::mixNativeSample()
{
    const int pulse = m_pulse1.output() + m_pulse2.output();
    const int tnd = 3 * m_triangle.output() + 2 * m_noise.output()
        + m_dmc.deltaCounter;
    const std::int32_t mixed =
        sPulseTable[static_cast<std::size_t>(pulse)]
        + sTndTable[static_cast<std::size_t>(tnd % 203)];
    m_native.push_back(static_cast<std::int16_t>(clamp16(mixed)));
}

// --- Stepping --------------------------------------------------------------------------

void Apu::stepApuCycle()
{
    // Pulse timers.
    for (int i = 0; i < 2; ++i) {
        Pulse& p = i == 0 ? m_pulse1 : m_pulse2;
        if (p.timer == 0) {
            p.timer = p.timerReload;
            p.dutyIndex = static_cast<std::uint8_t>((p.dutyIndex + 1) & 7);
        } else {
            --p.timer;
        }
    }
    // Triangle runs at the CPU rate: two steps per APU cycle.
    for (int i = 0; i < 2; ++i) {
        if (m_triangle.timer == 0) {
            m_triangle.timer = m_triangle.timerReload;
            if (m_triangle.length > 0 && m_triangle.linear > 0) {
                m_triangle.index = static_cast<std::uint8_t>(
                    (m_triangle.index + 1) & 31);
            }
        } else {
            --m_triangle.timer;
        }
    }
    // Noise.
    if (m_noise.timer == 0) {
        m_noise.timer = m_noise.timerReload;
        const std::uint16_t bit = m_noise.mode
            ? static_cast<std::uint16_t>((m_noise.lfsr >> 6) & 1)
            : static_cast<std::uint16_t>((m_noise.lfsr >> 1) & 1);
        const std::uint16_t feedback = static_cast<std::uint16_t>(
            (m_noise.lfsr & 1) ^ bit);
        m_noise.lfsr = static_cast<std::uint16_t>(
            (m_noise.lfsr >> 1) | (feedback << 14));
    } else {
        --m_noise.timer;
    }
    clockDmcTimer();

    mixNativeSample();
}

// Frame-sequencer checkpoints fire at exact master times derived from the
// sequence base (the $4017 write time), so odd CPU-cycle offsets are
// reachable (TIMING_MODEL.md; the 5-len_timing window is 1 CPU cycle wide).
// Clock events (quarter/half frames) per mode with their CPU-cycle offsets.
// The IRQ flag set (mode 0, cycle 29830) is scheduled separately so the
// restart skew cannot reorder the merged stream.
namespace {
struct SeqEvent {
    std::uint16_t cpu;
    bool quarter, half, wrap;
};
const SeqEvent kClocks0[4] = {
    {7457, true, false, false},
    {14913, true, true, false},
    {22371, true, false, false},
    {29829, true, true, true},
};
const SeqEvent kClocks5[4] = {
    {7457, true, false, false},
    {14913, true, true, false},
    {22371, true, false, false},
    {37281, true, true, true},
};
}

std::uint64_t Apu::nextSequenceEvent() const
{
    std::uint64_t best = ~static_cast<std::uint64_t>(0);
    if (m_sequenceStep < 4) {
        const SeqEvent& e = m_frameMode5 ? kClocks5[m_sequenceStep]
                                         : kClocks0[m_sequenceStep];
        best = m_seqClockBase + e.cpu * m_profile.cpuDivisor;
    }
    const std::uint64_t period = (m_frameMode5 ? 37281u : 29830u)
        * m_profile.cpuDivisor + (m_frameMode5 ? 6u : 0u);
    if (!m_frameMode5 && !m_irqFiredThisPeriod) {
        const std::uint64_t irq =
            m_seqBaseMaster + 29830u * m_profile.cpuDivisor;
        if (irq < best) {
            best = irq;
        }
    }
    // The period wraps at its end, AFTER the IRQ event, so a free-running
    // sequencer (power-on, no write) keeps its IRQ.
    if (m_sequenceStep == 4) {
        const std::uint64_t wrapAt = m_seqBaseMaster + period;
        if (wrapAt < best) {
            best = wrapAt;
        }
    }
    return best;
}

void Apu::fireSequenceEvent(std::uint64_t eventMaster)
{
    const std::uint64_t irqAt =
        m_seqBaseMaster + 29830u * m_profile.cpuDivisor;
    if (!m_frameMode5 && !m_irqFiredThisPeriod && eventMaster == irqAt) {
        m_irqFiredThisPeriod = true;
        if (!m_inhibitIrq) {
            // The flag set quantizes up to the APU's cycle grid: a 1-cycle
            // write shift flips which side of the grid a read lands on
            // (4-jitter tests 4/5).
            const std::uint64_t apuPeriod =
                static_cast<std::uint64_t>(m_profile.cpuDivisor) * 2;
            m_frameIrqAt =
                (eventMaster + timing::kFrameIrqVisibleTicks + apuPeriod - 1)
                / apuPeriod * apuPeriod;
            m_frameIrqAssertUntil = m_frameIrqAt + timing::kFrameIrqAssertTicks;
        }
        return;
    }
    const std::uint64_t period = (m_frameMode5 ? 37281u : 29830u)
        * m_profile.cpuDivisor + (m_frameMode5 ? 6u : 0u);
    if (eventMaster == m_seqBaseMaster + period) {
        // Period wrap: an event of its own, after the IRQ.
        m_seqBaseMaster += period;
        m_seqClockBase += period;
        m_sequenceStep = 0;
        m_irqFiredThisPeriod = false;
        return;
    }
    const SeqEvent& e = m_frameMode5 ? kClocks5[m_sequenceStep]
                                     : kClocks0[m_sequenceStep];
    ++m_sequenceStep;
    if (e.quarter) quarterFrame();
    if (e.half) halfFrame();
}

void Apu::runTo(std::uint64_t masterClock)
{
    const std::uint64_t apuPeriod =
        static_cast<std::uint64_t>(m_profile.cpuDivisor) * 2; // APU = CPU/2
    while (true) {
        const std::uint64_t evt = nextSequenceEvent();
        const std::uint64_t limit = evt < masterClock ? evt : masterClock;
        while ((m_apuCycles + 1) * apuPeriod <= limit) {
            stepApuCycle();
            ++m_apuCycles;
        }
        if (evt > masterClock) {
            break;
        }
        fireSequenceEvent(evt);
    }
}

// --- Resampling ------------------------------------------------------------------------

void Apu::flushToSink()
{
    if (m_sink == nullptr || m_native.empty()) {
        m_native.clear();
        return;
    }
    // Native rate = masterClockRate/2 (integer rational: num/(2*den) Hz).
    // Output j at time j/out lies between input i=floor(j*native/out).
    const std::uint64_t nativeNum =
        m_profile.masterClockNum
        / static_cast<std::uint64_t>(m_profile.cpuDivisor * 2);
    const std::uint64_t nativeDen = m_profile.masterClockDen;
    const std::uint64_t inCount = m_historyCount
        + static_cast<std::uint64_t>(m_native.size());

    m_outChunk.clear();
    while (true) {
        // Enough history for 16 taps centered after input i?
        const std::uint64_t j = m_nextOutNum;
        const std::uint64_t jn = j * nativeNum; // time*j in native ticks
        const std::uint64_t i = jn / (nativeDen * m_outRate);
        if (i + 8 >= inCount) {
            break;
        }
        const std::uint64_t rem = jn % (nativeDen * m_outRate);
        const int phase = static_cast<int>(
            rem * 64 / (nativeDen * m_outRate));

        long long acc = 0;
        for (int t = 0; t < 16; ++t) {
            // History[15] is the newest; input index (i - 7 + t).
            const std::int64_t idx = static_cast<std::int64_t>(i) - 7 + t;
            std::int16_t sample = 0;
            if (idx >= 0 && idx < static_cast<std::int64_t>(inCount)) {
                const std::int64_t fromNative =
                    idx - (static_cast<std::int64_t>(inCount)
                        - static_cast<std::int64_t>(m_native.size()));
                sample = fromNative >= 0
                    ? m_native[static_cast<std::size_t>(fromNative)]
                    : m_history[static_cast<std::size_t>(
                          16 + fromNative)];
            }
            acc += static_cast<long long>(sample)
                * sTaps[static_cast<std::size_t>(phase) * 16
                + static_cast<std::size_t>(t)];
        }
        // One-pole filters (fixed point, Q30) — HPF 90 Hz, HPF 440 Hz,
        // LPF 14 kHz at the output rate.
        const std::int32_t raw = static_cast<std::int32_t>(acc >> 14);
        const auto hpf = [](std::int64_t& z, std::int64_t& y,
                              std::int64_t a, std::int32_t x) {
            y = ((a * (y + x - z)) >> 30);
            z = x;
            return y;
        };
        std::int32_t v = raw;
        v = static_cast<std::int32_t>(hpf(m_hpf1, m_hpf1A, m_hpf1B, v));
        v = static_cast<std::int32_t>(hpf(m_hpf2, m_hpf2A, m_hpf2B, v));
        m_lpfA += ((m_lpfB * (v - m_lpfA)) >> 30);
        v = static_cast<std::int32_t>(m_lpfA);

        m_outChunk.push_back(static_cast<std::int16_t>(clamp16(v)));
        ++m_nextOutNum;
        if (m_outChunk.size() >= 4096) {
            m_sink->onSamples(m_outChunk.data(), m_outChunk.size(),
                m_sched.now());
            m_outChunk.clear();
        }
    }

    if (!m_outChunk.empty()) {
        m_sink->onSamples(m_outChunk.data(), m_outChunk.size(),
            m_sched.now());
    }

    // Keep the last 16 native samples as history for the next flush.
    std::array<std::int16_t, 16> next{};
    const std::size_t n = m_native.size();
    for (std::size_t k = 0; k < 16; ++k) {
        const std::int64_t idx = static_cast<std::int64_t>(n) - 16
            + static_cast<std::int64_t>(k);
        next[k] = idx >= 0 ? m_native[static_cast<std::size_t>(idx)]
            : m_history[static_cast<std::size_t>(16 + idx)];
    }
    m_history = next;
    m_historyCount += static_cast<std::int64_t>(n) < 0
        ? 0
        : static_cast<std::uint64_t>(n);
    m_native.clear();
}

} // namespace cutenes
