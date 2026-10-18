// 2A03 APU implementation (task T024; tests T017).
//
// Integer-only emulated state. The mixer uses the nesdev APU_Mixer lookup
// approximations (31-entry pulse, 203-entry TND) built once in double;
// runtime mixing is int32 adds + clamp, so audio is byte-identical across
// platforms (spec FR-012). DMC/OAM DMA CPU-stall accounting is refined in
// task T037; PAL APU period tables land with PAL timing validation.

#include "cutenes/apu/apu.hpp"

#include <cmath>

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
    m_sequenceCycle = 0;
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
            m_frameIrqAt = m_sched.now() + 6;
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
        m_pulse1.enabled = (value & 1) != 0;
        m_pulse2.enabled = (value & 2) != 0;
        m_triangle.enabled = (value & 4) != 0;
        m_noise.enabled = (value & 8) != 0;
        const bool dmcOn = (value & 0x10) != 0;
        if (!dmcOn) {
            m_dmc.bytesRemaining = 0;
        } else if (m_dmc.bytesRemaining == 0) {
            m_dmc.currentAddr = static_cast<std::uint16_t>(0xC000
                + m_dmc.addrReg * 64);
            m_dmc.bytesRemaining = static_cast<std::uint16_t>(
                m_dmc.lenReg * 16 + 1);
        }
        m_dmc.enabled = dmcOn;
        m_dmcIrq = false;
    } else if (addr == 0x4017) {
        m_frameMode5 = (value & 0x80) != 0;
        m_inhibitIrq = (value & 0x40) != 0;
        if (m_inhibitIrq) {
            m_frameIrqAt = 0;
            m_frameIrqAssertUntil = 0;
        }
        m_sequenceCycle = 0;
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
static void sequencePoint(std::uint32_t step, bool mode5,
    bool& quarter, bool& half, bool& irqReset)
{
    quarter = false;
    half = false;
    irqReset = false;
    if (mode5) {
        switch (step) {
        case 1: case 3: quarter = true; break;
        case 2: quarter = half = true; break;
        case 4: break; // idle
        case 5: quarter = half = irqReset = true; break;
        default: break;
        }
    } else {
        switch (step) {
        case 1: case 3: quarter = true; break;
        case 2: quarter = half = true; break;
        case 4: quarter = half = irqReset = true; break;
        default: break;
        }
    }
}

// --- DMC -----------------------------------------------------------------------------

void Apu::dmcFetchByte()
{
    // Reads via the bus without CPU-stall accounting (task T037).
    m_dmc.sampleBuffer = m_bus.read(m_dmc.currentAddr);
    m_dmc.bufferEmpty = false;
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
        m_dmc.timer = static_cast<std::uint16_t>(kDmcRates[m_dmc.rateIndex]
            / 2); // APU cycles between output clocks
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
            }
        }
    } else {
        --m_dmc.timer;
    }
    if (m_dmc.bufferEmpty && m_dmc.bytesRemaining > 0) {
        dmcFetchByte();
    }
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

    m_sequenceCycle += 2;
    // Frame-counter sequence checkpoints (CPU cycles: 2 per APU cycle).
    static const std::uint64_t k4[4] = {7457, 14913, 22371, 29830};
    static const std::uint64_t k5[5] = {7457, 14913, 22371, 29829, 37281};
    const std::uint64_t next = m_sequenceStep < 4
        ? (m_frameMode5 ? k5[m_sequenceStep] : k4[m_sequenceStep])
        : 0;
    if (m_sequenceCycle >= next && next != 0) {
        ++m_sequenceStep;
        bool quarter = false, half = false, irqReset = false;
        sequencePoint(static_cast<std::uint32_t>(m_sequenceStep),
            m_frameMode5, quarter, half, irqReset);
        if (quarter) quarterFrame();
        if (half) halfFrame();
        if (irqReset) {
            if (!m_inhibitIrq && !m_frameMode5) {
                // The flag becomes visible one CPU cycle after the APU
                // boundary (the hardware's odd/even parity, calibrated to
                // blargg's windows: clear at 29830, set at 29831).
                // Timestamp from THIS APU step (the scheduler may be far
                // ahead after a bulk advance), then +2 CPU cycles for the
                // flag's visibility parity.
                const std::uint64_t stepMaster = (m_apuCycles + 1)
                    * static_cast<std::uint64_t>(m_profile.cpuDivisor) * 2;
                // Calibration (6-irq_flag_timing, mid-cycle read sampling):
                // visible-from = fire + half a CPU cycle; condition active
                // until fire + 2 CPU cycles; re-armed flag visible half a
                // cycle after the clearing read.
                m_frameIrqAt = stepMaster + 24;
                m_frameIrqAssertUntil = m_frameIrqAt + 30;
            }
            m_sequenceCycle = 0;
            m_sequenceStep = 0;
        }
    }
}

void Apu::runTo(std::uint64_t masterClock)
{
    const std::uint64_t target =
        masterClock / (m_profile.cpuDivisor * 2); // APU = CPU/2
    while (m_apuCycles < target) {
        stepApuCycle();
        ++m_apuCycles;
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
