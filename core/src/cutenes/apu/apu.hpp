#pragma once

#include "cutenes/bus/bus.hpp"
#include "cutenes/system/scheduler.hpp"

#include <cutenes/media.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace cutenes {

class StateReader;
class StateWriter;

// The 2A03 APU: five channels stepped per APU cycle (CPU/2), integer LUT
// mixing, and an in-core fixed-point polyphase resampler delivering int16
// mono at the consumer-configured rate (research.md D8). Emulated state is
// integer-only; double math appears solely in one-time table construction.
//
// US1 scope note: DMC/OAM DMA cycle-stall accounting against the CPU is
// refined in task T037; the frame-counter write delay lands with T036.
class Apu final : public Tickable {
public:
    Apu(const RegionProfile& profile, Scheduler& scheduler, Bus& bus);

    void reset(bool cold);

    // Save-state section "APU " (task T049).
    void saveState(class StateWriter& w) const;
    void loadState(StateReader& r);

    // $4000-$4017 (the machine's APU/IO bridge dispatches here).
    std::uint8_t registerRead(std::uint16_t addr);
    void registerWrite(std::uint16_t addr, std::uint8_t value);

    void runTo(std::uint64_t masterClock) override;

    void setOutputRate(std::uint32_t rate);
    void setAudioSink(IAudioSink* sink);

    // Resample all pending native samples and push them to the sink (the
    // machine calls this once per frame).
    void flushToSink();

    // Mixer table introspection for tests (monotonicity/zero checks).
    static std::int32_t mixerPulse(int sum);
    static std::int32_t mixerTnd(int index);

    bool frameIrqPending() const // $4015 read visibility (fire + 2 cyc)
    {
        return m_frameIrqAt != 0 && m_sched.now() >= m_frameIrqAt;
    }

    // IRQ line level for the CPU poll: asserts AT the fire moment.
    bool frameIrqLine() const
    {
        return m_frameIrqAt != 0 && m_sched.now() >= m_frameIrqAt;
    }
    bool dmcIrqPending() const { return m_dmcIrq; }

    // CPU-serviced DMC memory fetch: the delta channel halts the processor
    // for 4 cycles while it reads its next sample byte (task T037).
    bool dmcFetchPending() const override
    {
        // A fetch is due once its DMA halt is scheduled: loads at the
        // $4015 write, reloads at the buffer drain — and, critically, at
        // the LAST-BIT wrap of the playing byte (tail arm): the DMA runs
        // during the byte's final timer period so the refill is present
        // AT the consume edge (hardware same-cycle full_after=1; T037).
        // Reload DMAs halt on put cycles (parity-gated). A load DMA
        // refused by the triggering write enters on the next READ cycle
        // whichever half it is (wiki DMA; Disch t=14120).
        //
        // Arbiter phase: the first half of an APU cycle is a get, so CPU
        // cycle 0 after reset is a GET slot — even CPU cycles are get and
        // odd are put. Reload DMA therefore halts on an odd (put) cycle.
        // The absolute power-on phase is arbitrary on hardware; this sense
        // is the one the hardware-validated reference tables were captured
        // with (the OAM arbiter in machine_impl.hpp uses the same grid).
        return m_dmc.enabled && m_dmc.bufferEmpty && m_dmc.bytesRemaining > 0
            && m_sched.now() >= m_dmcDueMaster
            && (!m_dmcHaltOnPut
                || ((m_sched.now() / 12) % 2 == 1));
    }
    // True when the pending fetch is a reload (4-cycle span); loads span 3.
    bool dmcIsReloadFetch() const override { return m_dmcHaltOnPut; }
    bool dmcGetWanted() const override
    {
        return m_dmc.enabled && m_dmc.bufferEmpty
            && m_dmc.bytesRemaining > 0 && m_sched.now() >= m_dmcDueMaster;
    }
    std::uint16_t dmcFetchAddress() const override
    {
        return m_dmc.currentAddr;
    }
    // True when the pending fetch is a reload DMA (parity-gated halt,
    // alignment cycle in the CPU stall). Loads enter on the first read.
    bool dmcHaltOnPutForAlign() const override { return m_dmcHaltOnPut; }
    void dmcDeliver(std::uint8_t byte) override
    {
        m_dmc.sampleBuffer = byte;
        m_dmc.bufferEmpty = false;
        advanceDmcAddress();
    }

private:
    struct Pulse {
        bool enabled = false;
        std::uint16_t timerReload = 0, timer = 0;
        std::uint8_t duty = 0, dutyIndex = 0;
        bool lengthEnabled = false;
        std::uint8_t length = 0;
        bool envStart = false, envLoop = false, envConstant = false;
        std::uint8_t envDiv = 0, envVol = 0, envDecay = 0;
        bool sweepEnabled = false, sweepReload = false, sweepNegate = false;
        std::uint8_t sweepPeriod = 0, sweepShift = 0, sweepDiv = 0;

        std::uint8_t output() const;
    };

    struct Triangle {
        bool enabled = false, control = false, reloadFlag = false;
        std::uint16_t timerReload = 0, timer = 0;
        std::uint8_t linearReload = 0, linear = 0, index = 0;
        bool lengthEnabled = false;
        std::uint8_t length = 0;

        std::uint8_t output() const;
    };

    struct Noise {
        bool enabled = false, mode = false;
        std::uint16_t timer = 0, timerReload = 0, periodIndex = 0;
        std::uint16_t lfsr = 1;
        bool lengthEnabled = false;
        std::uint8_t length = 0;
        bool envStart = false, envLoop = false, envConstant = false;
        std::uint8_t envDiv = 0, envVol = 0, envDecay = 0;

        std::uint8_t output() const;
    };

    struct Dmc {
        bool enabled = false, irqEnabled = false, loop = false;
        std::uint8_t rateIndex = 0;
        std::uint16_t timer = 0;
        std::uint16_t addrReg = 0, lenReg = 0;
        std::uint16_t currentAddr = 0;
        std::uint16_t bytesRemaining = 0;
        bool bufferEmpty = true;
        std::uint8_t sampleBuffer = 0, shiftReg = 0, bitsRemaining = 0;
        bool silence = true;
        std::uint8_t deltaCounter = 0;
    };

    void stepApuCycle();
    std::uint64_t nextSequenceEvent() const;
    void fireSequenceEvent(std::uint64_t eventMaster);
    void quarterFrame();
    void halfFrame();
    void write4017(std::uint8_t value);
    void clockEnvelope(Pulse& p);
    void clockSweep(Pulse& p, bool second);
    void clockLength(std::uint8_t& length, bool enabled);
    void clockDmcTimer();
    void dmcFetchByte();
    std::uint64_t m_dmcDueMaster = 0; // earliest halt for the next fetch
    bool m_dmcHaltOnPut = false; // reload DMAs halt on put, load on get
    void advanceDmcAddress();
    void mixNativeSample();
    std::uint8_t statusRead();

    const RegionProfile& m_profile;
    Scheduler& m_sched;
    Bus& m_bus;
    IAudioSink* m_sink = nullptr;

    Pulse m_pulse1, m_pulse2;
    Triangle m_triangle;
    Noise m_noise;
    Dmc m_dmc;

    // Frame counter.
    bool m_frameMode5 = false, m_inhibitIrq = false;
    std::uint8_t m_last4017 = 0; // rewrite-at-reset source (nesdev APU)
    bool m_dmcIrq = false;
    // Frame IRQ as master-clock timestamps (0 = none): visible-from and
    // condition-active-until (half-cycle parity + read re-assert).
    std::uint64_t m_frameIrqAt = 0;
    std::uint64_t m_frameIrqAssertUntil = 0;
    std::uint64_t m_seqBaseMaster = 0; // master time of the sequence base
    // Clock-event base: carries the one-time 3-cycle restart skew.
    std::uint64_t m_seqClockBase = 0;
    bool m_irqFiredThisPeriod = false;
    std::uint32_t m_sequenceStep = 0;

    std::uint64_t m_apuCycles = 0;

    // Native-rate mixed samples pending resampling.
    std::vector<std::int16_t> m_native;

    // Polyphase resampler (16 taps x 64 phases, Q14, built once in double).
    std::uint32_t m_outRate = 48000;
    std::uint64_t m_nextOutNum = 0; // output index j (j*out == time*native)
    std::array<std::int16_t, 16> m_history{};
    std::uint64_t m_historyCount = 0;
    std::vector<std::int16_t> m_outChunk;

    // Output filters as fixed-point one-poles (research.md D8), Q30.
    std::int64_t m_hpf1 = 0, m_hpf2 = 0;
    std::int64_t m_hpf1A = 0, m_hpf2A = 0, m_lpfA = 0;
    std::int64_t m_hpf1B = 0, m_hpf2B = 0, m_lpfB = 0;

    static const std::array<std::uint8_t, 32> kLengthTable;
    static const std::array<std::uint16_t, 16> kNoisePeriods;
    static const std::array<std::uint16_t, 16> kDmcRates;
    static const std::array<std::uint8_t, 4> kDuty;
    static std::array<std::int32_t, 31> sPulseTable;   // built once
    static std::array<std::int32_t, 203> sTndTable;    // built once
    static std::array<std::int32_t, 64 * 16> sTaps;    // Q14 sinc taps
    static bool sTablesBuilt;

    static void buildTables(); // one-time double-precision table init
};

} // namespace cutenes
