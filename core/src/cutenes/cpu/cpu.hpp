#pragma once

#include "cutenes/bus/bus.hpp"
#include "cutenes/system/scheduler.hpp"

#include <cutenes/state_view.hpp>

#include <cstdint>
#include <functional>

namespace cutenes {

class StateReader;
class StateWriter;

// The 2A03 CPU core: instruction-level 6502 with exact per-access cycle
// costs — every bus read/write is bracketed by half-cycle master-tick
// advances with PPU/APU catch-up (research.md D1). No BCD: the 2A03 D flag
// is ignored in ADC/SBC.
class Cpu {
public:
    enum class Mode : std::uint8_t {
        Imp, Acc, Imm, Zpg, Zpx, Zpy, Abs, Abx, Aby, Ind, Izx, Izy, Rel,
    };

    struct OpInfo {
        Mode mode;
        std::uint8_t cycles;
        bool pagePenalty; // +1 cycle when an indexed read crosses a page
    };

    // ppu/apu may be null (unit tests / headless component bring-up).
    Cpu(Bus& bus, Scheduler& scheduler, Tickable* ppu, Tickable* apu);

    // Opcode metadata for the full 256-entry space (documented + unofficial).
    static const OpInfo& opcodeInfo(std::uint8_t opcode);

    // cold=true: power-on (A=X=Y=0, S=$FD, P=$24); false: reset button
    // (S -= 3, I set, other registers preserved). Both re-fetch PC from
    // $FFFC/$FFFD consuming 7 CPU cycles (research.md D4).
    void reset(bool cold);

    // Execute one instruction, or service a pending interrupt first.
    void step();

    // Interrupt lines, sampled at instruction boundaries. NMI is
    // edge-triggered (the PPU calls assertNmi() exactly at the vblank dot).
    void assertNmi();
    void setIrq(bool level);
    // Live IRQ source, sampled at every CPU cycle start. When set, the
    // interrupt decision uses the line level at the second-to-last cycle of
    // the instruction (hardware polls IRQ in the penultimate cycle); when
    // unset, setIrq()'s boundary level is used (unit tests).
    void setIrqLineSource(std::function<bool()> source);

    CpuState state() const;

    // Register write surface (002 full control): mask bits 0=a 1=x 2=y
    // 3=s 4=p 5=pc; P bit 5 forced on, B never materialized.
    void writeRegisters(std::uint8_t mask, std::uint8_t a,
        std::uint8_t x, std::uint8_t y, std::uint8_t s,
        std::uint8_t p, std::uint16_t pc);

    // Harness/debug entry points (nestest automation enters at $C000).
    void setProgramCounter(std::uint16_t pc);
    void restartClockForTest();

    // Called at every instruction boundary (before the opcode fetch);
    // harness/debug surface, not part of the stable API.
    void setInstructionTrace(std::function<void(const CpuState&)> callback);

    // Save-state section "CPU " (task T049).
    void saveState(class StateWriter& w) const;
    void loadState(class StateReader& r);

private:
    std::uint8_t rd(std::uint16_t addr);
    void wr(std::uint16_t addr, std::uint8_t value);
    void tickHalf(bool phi2);
    void advanceDmcCycle(); // one stalled CPU cycle of a DMC DMA
    void serviceDmcDma(); // halt/dummy/[align]/get at the drain instant
    bool m_inDmcDma = false; // re-entrancy guard
    int m_dmcStallExtras = 0; // re-issue count for a DMC-stalled access
    bool m_dmaServicedThisHalf = false; // a DMA serviced at this cycle boundary
    std::uint8_t rawRead(std::uint16_t addr); // clocked read, no DMC re-entry
    void sampleNmi();
    // Per-cycle IRQ poll history: the decision at an instruction boundary
    // uses the sample from the start of its penultimate cycle.
    void sampleIrqCycle();
    void tick() { sampleIrqCycle(); tickHalf(false); tickHalf(false); sampleNmi(); }
    std::uint8_t fetch() { return rd(m_pc++); }

    void push(std::uint8_t value);
    std::uint8_t pop();

    // Value-returning indexed reads (the dummy read at the un-fixed address
    // is the 4th cycle; the +1 fix-up read happens only on page cross for
    // penalty ops). Address-returning write variants always take the dummy
    // read, then the caller writes the effective address.
    std::uint8_t readAbx(bool penalty);
    std::uint8_t readAby(bool penalty);
    std::uint8_t readIzx();
    std::uint8_t readIzy(bool penalty);
    std::uint16_t addrAbxWrite();
    std::uint16_t addrAbyWrite();
    std::uint16_t addrIzxWrite();
    std::uint16_t addrIzyWrite();

    void doInterrupt(std::uint16_t vector, bool brk);

    std::uint16_t readPair(); // fetch lo|hi<<8 (absolute address)
    std::uint16_t zpxAddr();  // zpg fetch + internal index cycle + wrap
    std::uint16_t zpyAddr();
    void compare(std::uint8_t reg, std::uint8_t value);

    // RMW addressing: (opcode & 0x1F) selects the mode within each family.
    std::uint16_t rmwAddr(std::uint8_t op);      // zpg/zpx/abs/abx group
    std::uint16_t rmwComboAddr(std::uint8_t op); // izx/zpg/abs/izy/zpx/aby/abx
    void execRmwCombo(std::uint8_t op);          // SLO/RLA/SRE/RRA/DCP/ISB

    void setNZ(std::uint8_t value);
    void adc(std::uint8_t value);
    std::uint8_t asl(std::uint8_t value);
    std::uint8_t lsr(std::uint8_t value);
    std::uint8_t rol(std::uint8_t value);
    std::uint8_t ror(std::uint8_t value);
    void branch(bool condition);

    Bus& m_bus;
    Scheduler& m_sched;
    Tickable* m_ppu;
    Tickable* m_apu;

    std::uint8_t m_a = 0;
    std::uint8_t m_x = 0;
    std::uint8_t m_y = 0;
    std::uint8_t m_s = 0xFD;
    std::uint8_t m_p = 0x24;
    std::uint16_t m_pc = 0;

    bool m_nmiPending = false;
    bool m_irqLevel = false;
    bool m_irqWasHigh = false;
    int m_irqEdgeCycle = -1;
    // Penultimate-cycle IRQ poll (see sampleIrqCycle()).
    std::function<bool()> m_irqLineSource;
    bool m_irqSampleCur = false;  // line at the start of the current cycle
    bool m_irqSamplePrev = false; // line at the start of the previous cycle
    bool m_irqPollHigh = false;   // penultimate-cycle sample of last instr
    int m_instrCycle = 0;         // normal CPU cycles in this instruction
    bool m_jammed = false;
    bool m_nmiWasHigh = false;
    int m_cycles = 0;              // 1-based cycle index within instruction
    int m_prevInstrCycles = 0;     // length of the last executed instruction
    int m_edgeCycle = 0;           // cycle the NMI edge latched on
    bool m_nmiStale = false;       // edge hit the last cycle: defer service
    bool m_iPrev = true;           // I flag at the previous instruction start
    bool m_iStart = true;
    bool m_branchSkipPoll = false; // taken branch: skip one boundary poll          // I flag at this instruction's start
    std::function<void(const CpuState&)> m_trace;
};

} // namespace cutenes
