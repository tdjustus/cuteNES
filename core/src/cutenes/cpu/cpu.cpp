// 6502/2A03 instruction-level core (tasks T019/T020).
//
// Cycle model: every rd()/wr() below is one CPU cycle (two half-cycle master
// ticks with PPU/APU catch-up); tick() is one internal cycle. Instruction
// cycle counts therefore fall out of the access pattern itself — the tables
// in kOps document them. Unofficial opcodes (LAX/SAX/DCP/ISB/SLO/RLA/SRE/
// RRA/NOPs + stable imm combos) follow the documented 2A03 behavior.

#include "cutenes/cpu/cpu.hpp"

#include "cutenes/serialize/save_state.hpp"
#include "cutenes/system/timing.hpp"

#include <array>
#include <utility>

namespace cutenes {

namespace {

constexpr auto Imp = Cpu::Mode::Imp;
constexpr auto Acc = Cpu::Mode::Acc;
constexpr auto Imm = Cpu::Mode::Imm;
constexpr auto Zpg = Cpu::Mode::Zpg;
constexpr auto Zpx = Cpu::Mode::Zpx;
constexpr auto Zpy = Cpu::Mode::Zpy;
constexpr auto Abs = Cpu::Mode::Abs;
constexpr auto Abx = Cpu::Mode::Abx;
constexpr auto Aby = Cpu::Mode::Aby;
constexpr auto Ind = Cpu::Mode::Ind;
constexpr auto Izx = Cpu::Mode::Izx;
constexpr auto Izy = Cpu::Mode::Izy;
constexpr auto Rel = Cpu::Mode::Rel;

constexpr bool P1 = true; // page-cross penalty applies

// 256-entry {mode, cycles, penalty} table (canonical 6502 timing).
constexpr std::array<Cpu::OpInfo, 256> kOps = {{
    {Imp, 7, 0},   // 00 BRK
    {Izx, 6, 0},   // 01 ORA
    {Imp, 2, 0},   // 02 KIL
    {Izx, 8, 0},   // 03 SLO
    {Zpg, 3, 0},   // 04 NOP
    {Zpg, 3, 0},   // 05 ORA
    {Zpg, 5, 0},   // 06 ASL
    {Zpg, 5, 0},   // 07 SLO
    {Imp, 3, 0},   // 08 PHP
    {Imm, 2, 0},   // 09 ORA
    {Acc, 2, 0},   // 0A ASL
    {Imm, 2, 0},   // 0B ANC
    {Abs, 4, 0},   // 0C NOP
    {Abs, 4, 0},   // 0D ORA
    {Abs, 6, 0},   // 0E ASL
    {Abs, 6, 0},   // 0F SLO
    {Rel, 2, P1},  // 10 BPL
    {Izy, 5, P1},  // 11 ORA
    {Imp, 2, 0},   // 12 KIL
    {Izy, 8, 0},   // 13 SLO
    {Zpx, 4, 0},   // 14 NOP
    {Zpx, 4, 0},   // 15 ORA
    {Zpx, 6, 0},   // 16 ASL
    {Zpx, 6, 0},   // 17 SLO
    {Imp, 2, 0},   // 18 CLC
    {Aby, 4, P1},  // 19 ORA
    {Imp, 2, 0},   // 1A NOP
    {Aby, 7, 0},   // 1B SLO
    {Abx, 4, P1},  // 1C NOP
    {Abx, 4, P1},  // 1D ORA
    {Abx, 7, 0},   // 1E ASL
    {Abx, 7, 0},   // 1F SLO
    {Abs, 6, 0},   // 20 JSR
    {Izx, 6, 0},   // 21 AND
    {Imp, 2, 0},   // 22 KIL
    {Izx, 8, 0},   // 23 RLA
    {Zpg, 3, 0},   // 24 BIT
    {Zpg, 3, 0},   // 25 AND
    {Zpg, 5, 0},   // 26 ROL
    {Zpg, 5, 0},   // 27 RLA
    {Imp, 4, 0},   // 28 PLP
    {Imm, 2, 0},   // 29 AND
    {Acc, 2, 0},   // 2A ROL
    {Imm, 2, 0},   // 2B ANC
    {Abs, 4, 0},   // 2C BIT
    {Abs, 4, 0},   // 2D AND
    {Abs, 6, 0},   // 2E ROL
    {Abs, 6, 0},   // 2F RLA
    {Rel, 2, P1},  // 30 BMI
    {Izy, 5, P1},  // 31 AND
    {Imp, 2, 0},   // 32 KIL
    {Izy, 8, 0},   // 33 RLA
    {Zpx, 4, 0},   // 34 NOP
    {Zpx, 4, 0},   // 35 AND
    {Zpx, 6, 0},   // 36 ROL
    {Zpx, 6, 0},   // 37 RLA
    {Imp, 2, 0},   // 38 SEC
    {Aby, 4, P1},  // 39 AND
    {Imp, 2, 0},   // 3A NOP
    {Aby, 7, 0},   // 3B RLA
    {Abx, 4, P1},  // 3C NOP
    {Abx, 4, P1},  // 3D AND
    {Abx, 7, 0},   // 3E ROL
    {Abx, 7, 0},   // 3F RLA
    {Imp, 6, 0},   // 40 RTI
    {Izx, 6, 0},   // 41 EOR
    {Imp, 2, 0},   // 42 KIL
    {Izx, 8, 0},   // 43 SRE
    {Zpg, 3, 0},   // 44 NOP
    {Zpg, 3, 0},   // 45 EOR
    {Zpg, 5, 0},   // 46 LSR
    {Zpg, 5, 0},   // 47 SRE
    {Imp, 3, 0},   // 48 PHA
    {Imm, 2, 0},   // 49 EOR
    {Acc, 2, 0},   // 4A LSR
    {Imm, 2, 0},   // 4B ALR
    {Abs, 3, 0},   // 4C JMP
    {Abs, 4, 0},   // 4D EOR
    {Abs, 6, 0},   // 4E LSR
    {Abs, 6, 0},   // 4F SRE
    {Rel, 2, P1},  // 50 BVC
    {Izy, 5, P1},  // 51 EOR
    {Imp, 2, 0},   // 52 KIL
    {Izy, 8, 0},   // 53 SRE
    {Zpx, 4, 0},   // 54 NOP
    {Zpx, 4, 0},   // 55 EOR
    {Zpx, 6, 0},   // 56 LSR
    {Zpx, 6, 0},   // 57 SRE
    {Imp, 2, 0},   // 58 CLI
    {Aby, 4, P1},  // 59 EOR
    {Imp, 2, 0},   // 5A NOP
    {Aby, 7, 0},   // 5B SRE
    {Abx, 4, P1},  // 5C NOP
    {Abx, 4, P1},  // 5D EOR
    {Abx, 7, 0},   // 5E LSR
    {Abx, 7, 0},   // 5F SRE
    {Imp, 6, 0},   // 60 RTS
    {Izx, 6, 0},   // 61 ADC
    {Imp, 2, 0},   // 62 KIL
    {Izx, 8, 0},   // 63 RRA
    {Zpg, 3, 0},   // 64 NOP
    {Zpg, 3, 0},   // 65 ADC
    {Zpg, 5, 0},   // 66 ROR
    {Zpg, 5, 0},   // 67 RRA
    {Imp, 4, 0},   // 68 PLA
    {Imm, 2, 0},   // 69 ADC
    {Acc, 2, 0},   // 6A ROR
    {Imm, 2, 0},   // 6B ARR
    {Ind, 5, 0},   // 6C JMP
    {Abs, 4, 0},   // 6D ADC
    {Abs, 6, 0},   // 6E ROR
    {Abs, 6, 0},   // 6F RRA
    {Rel, 2, P1},  // 70 BVS
    {Izy, 5, P1},  // 71 ADC
    {Imp, 2, 0},   // 72 KIL
    {Izy, 8, 0},   // 73 RRA
    {Zpx, 4, 0},   // 74 NOP
    {Zpx, 4, 0},   // 75 ADC
    {Zpx, 6, 0},   // 76 ROR
    {Zpx, 6, 0},   // 77 RRA
    {Imp, 2, 0},   // 78 SEI
    {Aby, 4, P1},  // 79 ADC
    {Imp, 2, 0},   // 7A NOP
    {Aby, 7, 0},   // 7B RRA
    {Abx, 4, P1},  // 7C NOP
    {Abx, 4, P1},  // 7D ADC
    {Abx, 7, 0},   // 7E ROR
    {Abx, 7, 0},   // 7F RRA
    {Imm, 2, 0},   // 80 NOP
    {Izx, 6, 0},   // 81 STA
    {Imm, 2, 0},   // 82 NOP
    {Izx, 6, 0},   // 83 SAX
    {Zpg, 3, 0},   // 84 STY
    {Zpg, 3, 0},   // 85 STA
    {Zpg, 3, 0},   // 86 STX
    {Zpg, 5, 0},   // 87 SAX
    {Imp, 2, 0},   // 88 DEY
    {Imm, 2, 0},   // 89 NOP
    {Imp, 2, 0},   // 8A TXA
    {Imm, 2, 0},   // 8B XAA
    {Abs, 4, 0},   // 8C STY
    {Abs, 4, 0},   // 8D STA
    {Abs, 4, 0},   // 8E STX
    {Abs, 4, 0},   // 8F SAX
    {Rel, 2, P1},  // 90 BCC
    {Izy, 6, 0},   // 91 STA
    {Imp, 2, 0},   // 92 KIL
    {Izy, 6, 0},   // 93 SHA
    {Zpx, 4, 0},   // 94 STY
    {Zpx, 4, 0},   // 95 STA
    {Zpy, 4, 0},   // 96 STX
    {Zpy, 4, 0},   // 97 SAX
    {Imp, 2, 0},   // 98 TYA
    {Aby, 5, 0},   // 99 STA
    {Imp, 2, 0},   // 9A TXS
    {Abx, 5, 0},   // 9B SHS
    {Abx, 5, 0},   // 9C SHY
    {Abx, 5, 0},   // 9D STA
    {Aby, 5, 0},   // 9E SHX
    {Aby, 5, 0},   // 9F SHA
    {Imm, 2, 0},   // A0 LDY
    {Izx, 6, 0},   // A1 LDA
    {Imm, 2, 0},   // A2 LDX
    {Izx, 6, 0},   // A3 LAX
    {Zpg, 3, 0},   // A4 LDY
    {Zpg, 3, 0},   // A5 LDA
    {Zpg, 3, 0},   // A6 LDX
    {Zpg, 3, 0},   // A7 LAX
    {Imp, 2, 0},   // A8 TAY
    {Imm, 2, 0},   // A9 LDA
    {Imp, 2, 0},   // AA TAX
    {Imm, 2, 0},   // AB LAX
    {Abs, 4, 0},   // AC LDY
    {Abs, 4, 0},   // AD LDA
    {Abs, 4, 0},   // AE LDX
    {Abs, 4, 0},   // AF LAX
    {Rel, 2, P1},  // B0 BCS
    {Izy, 5, P1},  // B1 LDA
    {Imp, 2, 0},   // B2 KIL
    {Izy, 5, P1},  // B3 LAX
    {Zpx, 4, 0},   // B4 LDY
    {Zpx, 4, 0},   // B5 LDA
    {Zpy, 4, 0},   // B6 LDX
    {Zpy, 4, 0},   // B7 LAX
    {Imp, 2, 0},   // B8 CLV
    {Aby, 4, P1},  // B9 LDA
    {Imp, 2, 0},   // BA TSX
    {Aby, 4, P1},  // BB LAS
    {Abx, 4, P1},  // BC LDY
    {Abx, 4, P1},  // BD LDA
    {Aby, 4, P1},  // BE LDX
    {Aby, 4, P1},  // BF LAX
    {Imm, 2, 0},   // C0 CPY
    {Izx, 6, 0},   // C1 CMP
    {Imm, 2, 0},   // C2 NOP
    {Izx, 8, 0},   // C3 DCP
    {Zpg, 3, 0},   // C4 CPY
    {Zpg, 3, 0},   // C5 CMP
    {Zpg, 5, 0},   // C6 DEC
    {Zpg, 5, 0},   // C7 DCP
    {Imp, 2, 0},   // C8 INY
    {Imm, 2, 0},   // C9 CMP
    {Imp, 2, 0},   // CA DEX
    {Imm, 2, 0},   // CB SBX
    {Abs, 4, 0},   // CC CPY
    {Abs, 4, 0},   // CD CMP
    {Abs, 6, 0},   // CE DEC
    {Abs, 6, 0},   // CF DCP
    {Rel, 2, P1},  // D0 BNE
    {Izy, 5, P1},  // D1 CMP
    {Imp, 2, 0},   // D2 KIL
    {Izy, 8, 0},   // D3 DCP
    {Zpx, 4, 0},   // D4 NOP
    {Zpx, 4, 0},   // D5 CMP
    {Zpx, 6, 0},   // D6 DEC
    {Zpx, 6, 0},   // D7 DCP
    {Imp, 2, 0},   // D8 CLD
    {Aby, 4, P1},  // D9 CMP
    {Imp, 2, 0},   // DA NOP
    {Aby, 7, 0},   // DB DCP
    {Abx, 4, P1},  // DC NOP
    {Abx, 4, P1},  // DD CMP
    {Abx, 7, 0},   // DE DEC
    {Abx, 7, 0},   // DF DCP
    {Imm, 2, 0},   // E0 CPX
    {Izx, 6, 0},   // E1 SBC
    {Imm, 2, 0},   // E2 NOP
    {Izx, 8, 0},   // E3 ISB
    {Zpg, 3, 0},   // E4 CPX
    {Zpg, 3, 0},   // E5 SBC
    {Zpg, 5, 0},   // E6 INC
    {Zpg, 5, 0},   // E7 ISB
    {Imp, 2, 0},   // E8 INX
    {Imm, 2, 0},   // E9 SBC
    {Imp, 2, 0},   // EA NOP
    {Imm, 2, 0},   // EB SBC
    {Abs, 4, 0},   // EC CPX
    {Abs, 4, 0},   // ED SBC
    {Abs, 6, 0},   // EE INC
    {Abs, 6, 0},   // EF ISB
    {Rel, 2, P1},  // F0 BEQ
    {Izy, 5, P1},  // F1 SBC
    {Imp, 2, 0},   // F2 KIL
    {Izy, 8, 0},   // F3 ISB
    {Zpx, 4, 0},   // F4 NOP
    {Zpx, 4, 0},   // F5 SBC
    {Zpx, 6, 0},   // F6 INC
    {Zpx, 6, 0},   // F7 ISB
    {Imp, 2, 0},   // F8 SED
    {Aby, 4, P1},  // F9 SBC
    {Imp, 2, 0},   // FA NOP
    {Aby, 7, 0},   // FB ISB
    {Abx, 4, P1},  // FC NOP
    {Abx, 4, P1},  // FD SBC
    {Abx, 7, 0},   // FE INC
    {Abx, 7, 0},   // FF ISB
}};

constexpr std::uint8_t kC = 0x01, kZ = 0x02, kI = 0x04, kD = 0x08;
constexpr std::uint8_t kB = 0x10, kU = 0x20, kV = 0x40, kN = 0x80;

} // namespace

const Cpu::OpInfo& Cpu::opcodeInfo(std::uint8_t opcode)
{
    return kOps[opcode];
}

Cpu::Cpu(Bus& bus, Scheduler& scheduler, Tickable* ppu, Tickable* apu)
    : m_bus(bus)
    , m_sched(scheduler)
    , m_ppu(ppu)
    , m_apu(apu)
{
}

void Cpu::tickHalf(bool /*phi2*/)
{
    m_sched.advanceMaster(m_sched.cpuCycleToMaster(1) / timing::kCpuHalvesPerCycle);
    const std::uint64_t now = m_sched.now();
    if (m_ppu != nullptr) {
        m_ppu->runTo(now);
    }
    if (m_apu != nullptr) {
        m_apu->runTo(now);
        // Per-cycle DMC DMA (task T037): the DMA unit is independent of the
        // instruction stream — when a fetch comes due (buffer drained at a
        // DMC tick during catch-up), it halts the CPU NOW and takes the bus:
        // halt, dummy, optional align, get, charged as real cycles.
        if (!m_inDmcDma && (now % m_sched.cpuCycleToMaster(1)) == 0
            && m_apu->dmcFetchPending()) {
            m_inDmcDma = true;
            m_dmaServicedThisHalf = true;
            serviceDmcDma();
            m_inDmcDma = false;
        }
    }
}

// The DMC DMA sequence: halt, dummy, optional align to a get cycle, get.
// Called from tickHalf at the drain instant (per-cycle engine, T037).
void Cpu::serviceDmcDma()
{
    // A $2007 access stalled by this DMA causes extra read-side bus
    // accesses (blargg dmc_dma_during_read4: reads get "2-3 extra $2007
    // reads before real read"; writes show the same buffer/v oddities
    // while the write itself lands once — "DMC DMA during $2007 write
    // has no effect"). rd() consumes the count at the next $2007 read.
    m_dmcStallExtras = 2;    advanceDmcCycle(); // halt cycle
    advanceDmcCycle(); // dummy
    if (m_apu->dmcIsReloadFetch()) {
        // Reloads span 4 (halt, dummy, align, get — blargg's fixed count).
        advanceDmcCycle(); // align
    }
    // Loads span 3 (halt, dummy, get — Disch: a load halted on a read
    // needs no alignment); with the next-boundary start that totals 4
    // from the $4015 write.
    const std::uint16_t dmcAddr = m_apu->dmcFetchAddress();
    const std::uint8_t byte = rawRead(dmcAddr); // the get
    m_apu->dmcDeliver(byte);
}

// One stalled CPU cycle of a DMC DMA: advance a full cycle with PPU/APU
// catch-up, bypassing rd()/wr() (no bus access on halt/dummy/align).
void Cpu::advanceDmcCycle()
{
    m_sched.advanceMaster(m_sched.cpuCycleToMaster(1));
    const std::uint64_t now = m_sched.now();
    if (m_ppu != nullptr) {
        m_ppu->runTo(now);
    }
    if (m_apu != nullptr) {
        m_apu->runTo(now);
    }
}

void Cpu::sampleNmi()
{
    if (m_ppu == nullptr) {
        return;
    }
    // The pins are sampled right after the bus access (during phi2), before
    // the cycle's second half advances time — a $2000 write that enables
    // NMI one cycle before the flag clear still raises the line in time
    // (07-nmi_on_timing rows 3-4).
    if (m_ppu != nullptr) {
        const bool line = m_ppu->nmiLine();
        if (line && !m_nmiWasHigh) {
            m_nmiPending = true;
            m_edgeCycle = m_cycles; // 1-based index within the instruction
        }
        m_nmiWasHigh = line;
    }
    if (m_irqLevel && !m_irqWasHigh) {
        m_irqEdgeCycle = m_cycles;
    }
    m_irqWasHigh = m_irqLevel != false;
}

std::uint8_t Cpu::rd(std::uint16_t addr)
{
    ++m_cycles;
    sampleIrqCycle();
    // A pending DMC fetch halts the CPU: ~3 stall cycles plus the clocked
    // memory read itself (task T037). Get-cycles here also stall sprite DMA.
    // DMC DMA service moved to tickHalf (per-cycle engine, task T037):
    // fetches are served at the drain instant, not at the next read.
    // Pre-access NMI sample: the silicon edge detector is continuous, so
    // the line must be observed after catch-up but BEFORE a $2002 read
    // clears the flag — a tight poll loop otherwise never latches the
    // edge (the Holy Mapperel vsync hang). The kill window still cancels
    // latched edges, preserving the suppression tests.
    if ((addr & 0x3FFF) == 0x2002) {
        sampleNmi();
    }
    tickHalf(false);
    // A DMC-stalled access re-issues on the bus as READS (the halt drives
    // the address bus; blargg dmc_dma_during_read4): $2007 refills the
    // read buffer and increments v twice ("2-3 extra $2007 reads") while
    // a stalled WRITE still lands its byte once ("no effect"); $4016/17
    // shift the controller once ("causes extra $4016 read"). The re-issue
    // attaches to the access whose cycle the halt boundary opened — the
    // flag is one-shot per service (the sync loop fetches continuously;
    // a stale flag would shift every later access).
    if (m_dmaServicedThisHalf) {
        const bool isVram = (addr & 0x3FFF) == 0x2007; // PPU data mirror
        const bool isPort = (addr & 0x401F) >= 0x4016
            && (addr & 0x401F) <= 0x4017; // controller ports
        if (isVram) {
            for (int i = 0; i < m_dmcStallExtras; ++i) {
                (void)m_bus.read(addr);
            }
        } else if (isPort) {
            (void)m_bus.read(addr);
        }
    }
    m_dmaServicedThisHalf = false;
    const std::uint8_t value = m_bus.read(addr);
    if ((addr & 0x3FFF) == 0x2002 && m_ppu != nullptr) {
        // A $2002 read in the suppression window cancels the frame's NMI.
        if (m_ppu->consumeNmiKill()) {
            m_nmiPending = false;
        }
    }
    sampleNmi();
    tickHalf(false);
    return value;
}

std::uint8_t Cpu::rawRead(std::uint16_t addr)
{
    tickHalf(false);
    const std::uint8_t value = m_bus.read(addr);
    sampleNmi();
    tickHalf(false);
    return value;
}

void Cpu::wr(std::uint16_t addr, std::uint8_t value)
{
    ++m_cycles;
    sampleIrqCycle();
    tickHalf(false);
    // A stalled $2007 WRITE re-issues as reads (buffer refill + v
    // increment) while the byte itself lands once — "DMC DMA during
    // $2007 write has no effect" on VRAM content (dmc_dma_during_read4).
    // Non-register writes pass the pending flag through (their re-read
    // is invisible).
    // A stalled $2007 WRITE re-issues as ONE read (buffer refill + one v
    // increment — the dump shows the byte landing one address later with
    // the pre-write buffer value) while the byte itself lands once — "DMC
    // DMA during $2007 write has no effect" on the write itself
    // (dmc_dma_during_read4; expected row 22 11 22 AA 44..).
    if (m_dmaServicedThisHalf && (addr & 0x3FFF) == 0x2007) {
        (void)m_bus.read(addr);
    }
    m_dmaServicedThisHalf = false;
    m_bus.write(addr, value);
    sampleNmi();
    tickHalf(false);
}

void Cpu::push(std::uint8_t value)
{
    wr(static_cast<std::uint16_t>(0x100 | m_s), value);
    --m_s;
}

std::uint8_t Cpu::pop()
{
    ++m_s;
    return rd(static_cast<std::uint16_t>(0x100 | m_s));
}

void Cpu::setNZ(std::uint8_t value)
{
    m_p = static_cast<std::uint8_t>(
        (m_p & ~(kN | kZ)) | (value & kN) | ((value == 0) ? kZ : 0));
}

void Cpu::adc(std::uint8_t value)
{
    const unsigned r = m_a + value + (m_p & kC);
    const std::uint8_t result = static_cast<std::uint8_t>(r);
    const std::uint8_t overflow =
        static_cast<std::uint8_t>(
            ((~(m_a ^ value) & (m_a ^ result)) & kN) != 0 ? kV : 0);
    m_p = static_cast<std::uint8_t>((m_p & ~(kC | kV))
        | ((r > 0xFF) ? kC : 0) | overflow);
    setNZ(result);
    m_a = result;
}

std::uint8_t Cpu::asl(std::uint8_t value)
{
    m_p = static_cast<std::uint8_t>((m_p & ~kC) | ((value >> 7) & 1));
    value = static_cast<std::uint8_t>(value << 1);
    setNZ(value);
    return value;
}

std::uint8_t Cpu::lsr(std::uint8_t value)
{
    m_p = static_cast<std::uint8_t>((m_p & ~kC) | (value & 1));
    value = static_cast<std::uint8_t>(value >> 1);
    setNZ(value);
    return value;
}

std::uint8_t Cpu::rol(std::uint8_t value)
{
    const std::uint8_t carryIn = static_cast<std::uint8_t>(m_p & kC);
    m_p = static_cast<std::uint8_t>((m_p & ~kC) | ((value >> 7) & 1));
    value = static_cast<std::uint8_t>((value << 1) | carryIn);
    setNZ(value);
    return value;
}

std::uint8_t Cpu::ror(std::uint8_t value)
{
    const std::uint8_t carryIn = static_cast<std::uint8_t>(m_p & kC);
    m_p = static_cast<std::uint8_t>((m_p & ~kC) | (value & 1));
    value = static_cast<std::uint8_t>((value >> 1) | (carryIn << 7));
    setNZ(value);
    return value;
}

std::uint16_t Cpu::readPair()
{
    const std::uint16_t lo = fetch();
    const std::uint16_t hi = fetch();
    return static_cast<std::uint16_t>(lo | (hi << 8));
}

std::uint16_t Cpu::zpxAddr()
{
    const std::uint16_t a = fetch();
    tick(); // internal index-addition cycle (zpg,X/Y data ops are 4 cycles)
    return static_cast<std::uint16_t>((a + m_x) & 0xFF);
}

std::uint16_t Cpu::zpyAddr()
{
    const std::uint16_t a = fetch();
    tick();
    return static_cast<std::uint16_t>((a + m_y) & 0xFF);
}

std::uint8_t Cpu::readAbx(bool penalty)
{
    const std::uint16_t lo = fetch();
    const std::uint16_t hi = fetch();
    const std::uint16_t base = static_cast<std::uint16_t>(lo | (hi << 8));
    const std::uint16_t unfixed =
        static_cast<std::uint16_t>((base & 0xFF00) | ((lo + m_x) & 0xFF));
    const std::uint8_t value = rd(unfixed); // 4th cycle: un-fixed address
    if (penalty && (((base ^ (base + m_x)) & 0xFF00) != 0)) {
        return rd(static_cast<std::uint16_t>(base + m_x));
    }
    return value;
}

std::uint8_t Cpu::readAby(bool penalty)
{
    const std::uint16_t lo = fetch();
    const std::uint16_t hi = fetch();
    const std::uint16_t base = static_cast<std::uint16_t>(lo | (hi << 8));
    const std::uint16_t unfixed =
        static_cast<std::uint16_t>((base & 0xFF00) | ((lo + m_y) & 0xFF));
    const std::uint8_t value = rd(unfixed);
    if (penalty && (((base ^ (base + m_y)) & 0xFF00) != 0)) {
        return rd(static_cast<std::uint16_t>(base + m_y));
    }
    return value;
}

std::uint8_t Cpu::readIzx()
{
    const std::uint8_t z = fetch();
    tick(); // index addition
    const std::uint16_t ptr = static_cast<std::uint16_t>((z + m_x) & 0xFF);
    const std::uint8_t lo = rd(ptr);
    const std::uint8_t hi = rd(
        static_cast<std::uint16_t>((ptr + 1) & 0xFF)); // zpg wrap
    return rd(static_cast<std::uint16_t>(lo | (hi << 8)));
}

std::uint8_t Cpu::readIzy(bool penalty)
{
    const std::uint8_t z = fetch();
    const std::uint8_t lo = rd(z);
    const std::uint8_t hi = rd(
        static_cast<std::uint16_t>((z + 1) & 0xFF)); // zpg wrap
    const std::uint16_t base = static_cast<std::uint16_t>(lo | (hi << 8));
    const std::uint16_t unfixed =
        static_cast<std::uint16_t>((base & 0xFF00) | ((lo + m_y) & 0xFF));
    const std::uint8_t value = rd(unfixed);
    if (penalty && (((base ^ (base + m_y)) & 0xFF00) != 0)) {
        return rd(static_cast<std::uint16_t>(base + m_y));
    }
    return value;
}

std::uint16_t Cpu::addrAbxWrite()
{
    const std::uint16_t lo = fetch();
    const std::uint16_t hi = fetch();
    const std::uint16_t base = static_cast<std::uint16_t>(lo | (hi << 8));
    (void)rd(static_cast<std::uint16_t>(
        (base & 0xFF00) | ((lo + m_x) & 0xFF))); // internal/dummy cycle
    return static_cast<std::uint16_t>(base + m_x);
}

std::uint16_t Cpu::addrAbyWrite()
{
    const std::uint16_t lo = fetch();
    const std::uint16_t hi = fetch();
    const std::uint16_t base = static_cast<std::uint16_t>(lo | (hi << 8));
    (void)rd(static_cast<std::uint16_t>(
        (base & 0xFF00) | ((lo + m_y) & 0xFF)));
    return static_cast<std::uint16_t>(base + m_y);
}

std::uint16_t Cpu::addrIzxWrite()
{
    const std::uint8_t z = fetch();
    tick();
    const std::uint16_t ptr = static_cast<std::uint16_t>((z + m_x) & 0xFF);
    const std::uint8_t lo = rd(ptr);
    const std::uint8_t hi = rd(
        static_cast<std::uint16_t>((ptr + 1) & 0xFF)); // zpg wrap
    return static_cast<std::uint16_t>(lo | (hi << 8));
}

std::uint16_t Cpu::addrIzyWrite()
{
    const std::uint8_t z = fetch();
    const std::uint8_t lo = rd(z);
    const std::uint8_t hi = rd(
        static_cast<std::uint16_t>((z + 1) & 0xFF)); // zpg wrap
    const std::uint16_t base = static_cast<std::uint16_t>(lo | (hi << 8));
    (void)rd(static_cast<std::uint16_t>(
        (base & 0xFF00) | ((lo + m_y) & 0xFF)));
    return static_cast<std::uint16_t>(base + m_y);
}

// Address for the standard zpg/zpx/abs/abx RMW group (ASL/LSR/ROL/ROR/
// INC/DEC): (opcode & 0x1F) is 06 zpg, 16 zpx, 0E abs, 1E abx.
std::uint16_t Cpu::rmwAddr(std::uint8_t op)
{
    switch (op & 0x1F) {
    case 0x06: return fetch();
    case 0x16: {
        const std::uint16_t a = fetch();
        tick();
        return static_cast<std::uint16_t>(a + m_x) & 0xFF;
    }
    case 0x0E: return readPair();
    default: return addrAbxWrite(); // 0x1E
    }
}

// Address for the unofficial RMW-combo group: (opcode & 0x1F) is 03 izx,
// 07 zpg, 0F abs, 13 izy, 17 zpx, 1B aby, 1F abx.
std::uint16_t Cpu::rmwComboAddr(std::uint8_t op)
{
    switch (op & 0x1F) {
    case 0x03: return addrIzxWrite();
    case 0x07: return fetch();
    case 0x0F: return readPair();
    case 0x13: return addrIzyWrite();
    case 0x17: {
        const std::uint16_t a = fetch();
        tick();
        return static_cast<std::uint16_t>((a + m_x) & 0xFF); // zpg wrap
    }
    case 0x1B: return addrAbyWrite();
    default: return addrAbxWrite(); // 0x1F
    }
}

void Cpu::branch(bool condition)
{
    const std::int8_t offset = static_cast<std::int8_t>(fetch());
    if (!condition) {
        return;
    }
    tick(); // taken: extra cycle
    const std::uint16_t target = static_cast<std::uint16_t>(m_pc + offset);
    const bool crossed = (target & 0xFF00) != (m_pc & 0xFF00);
    if (crossed) {
        tick(); // page cross: one more
    }
    // A taken non-page-crossing branch ignores IRQ/NMI during its last
    // clock, so the next instruction runs before service (test 5).
    m_branchSkipPoll = !crossed;
    m_pc = target;
}

void Cpu::doInterrupt(std::uint16_t vector, bool brk)
{
    if (!brk) {
        tick();
        tick(); // two dummy cycles before the pushes
    } else {
        (void)fetch(); // BRK signature byte read; PC is now +2
    }
    push(static_cast<std::uint8_t>(m_pc >> 8));
    push(static_cast<std::uint8_t>(m_pc & 0xFF));
    push(static_cast<std::uint8_t>(m_p | kU | (brk ? kB : 0)));
    m_p |= kI;
    // NMI hijack (nesdev "CPU interrupts"; blargg cpu_interrupts_v2
    // 2-nmi_and_brk / 3-nmi_and_irq): an NMI edge latched by the end of
    // the status push redirects the vector fetch to the NMI vector — the
    // pushed flags keep their B value (set for a hijacked BRK) and only
    // one interrupt sequence runs. Edges latched during the vector fetch
    // cycles themselves land after this check and are taken normally
    // after the sequence.
    if (vector != 0xFFFA && m_nmiPending) {
        vector = 0xFFFA;
        m_nmiPending = false;
        m_nmiStale = false;
    }
    const std::uint8_t lo = rd(static_cast<std::uint16_t>(vector));
    const std::uint8_t hi = rd(static_cast<std::uint16_t>(vector + 1));
    m_pc = static_cast<std::uint16_t>(lo | (hi << 8));
}

void Cpu::reset(bool cold)
{
    if (cold) {
        m_a = m_x = m_y = 0;
        m_s = 0xFD;
        m_p = kI | kU; // $24
    } else {
        m_s = static_cast<std::uint8_t>(m_s - 3);
        m_p |= kI;
    }
    m_nmiPending = false;
    m_jammed = false;
    m_irqPollHigh = false;
    m_irqSampleCur = false;
    m_irqSamplePrev = false;
    m_instrCycle = 0;

    // Reset sequence: internal cycles then the vector fetch. The absolute
    // first-fetch cycle sets the CPU-vs-PPU/DMC phase for the whole run;
    // 8 cycles (first fetch at CYC:8) matches the reference's
    // hardware-validated alignment (T037 session 25); nestest re-check below.
    tick();
    tick();
    tick();
    tick();
    tick();
    tick();
    const std::uint8_t lo = rd(0xFFFC);
    const std::uint8_t hi = rd(0xFFFD);
    m_pc = static_cast<std::uint16_t>(lo | (hi << 8));
}

void Cpu::assertNmi()
{
    m_nmiPending = true;
    m_edgeCycle = -1; // externally-asserted edges predate everything
}
void Cpu::setIrq(bool level) { m_irqLevel = level; }
void Cpu::setIrqLineSource(std::function<bool()> source)
{
    m_irqLineSource = std::move(source);
}

// Sample the live IRQ line at the start of a normal CPU cycle. DMA stall
// cycles (sprite/DMC) never reach this path, so they do not count toward the
// instruction's architectural length — the poll for `sta $4014` therefore
// lands on its hi-byte-fetch cycle, before the DMA stretches the write cycle.
void Cpu::sampleIrqCycle()
{
    m_irqSamplePrev = m_irqSampleCur;
    m_irqSampleCur = m_irqLineSource ? m_irqLineSource() : m_irqLevel;
    ++m_instrCycle;
}

CpuState Cpu::state() const
{
    return CpuState{m_a, m_x, m_y, m_s, m_p, m_pc, m_sched.now()};
}

void Cpu::writeRegisters(std::uint8_t mask, std::uint8_t newA,
    std::uint8_t newX, std::uint8_t newY, std::uint8_t newS,
    std::uint8_t newP, std::uint16_t newPc)
{
    if (mask & 0x01) m_a = newA;
    if (mask & 0x02) m_x = newX;
    if (mask & 0x04) m_y = newY;
    if (mask & 0x08) m_s = newS;
    if (mask & 0x10) {
        m_p = static_cast<std::uint8_t>((newP | kU) & ~kB); // file consts
    }
    if (mask & 0x20) m_pc = newPc;
}

void Cpu::setProgramCounter(std::uint16_t pc) { m_pc = pc; }

void Cpu::setInstructionTrace(std::function<void(const CpuState&)> callback)
{
    m_trace = std::move(callback);
}
void Cpu::restartClockForTest() { m_sched.reset(); }

void Cpu::compare(std::uint8_t reg, std::uint8_t value)
{
    const unsigned r = reg - value;
    m_p = static_cast<std::uint8_t>((m_p & ~kC) | ((reg >= value) ? kC : 0));
    setNZ(static_cast<std::uint8_t>(r));
}

// SLO/RLA/SRE/RRA/DCP/ISB families: read-modify-write with a follow-up
// accumulator operation. Family = opcode & 0xE0.
void Cpu::execRmwCombo(std::uint8_t op)
{
    const std::uint16_t a = rmwComboAddr(op);
    const std::uint8_t v = rd(a);
    wr(a, v); // NMOS double write: old value first
    std::uint8_t r = 0;
    switch (op & 0xE0) {
    case 0x00: // SLO: ASL memory, then ORA
        r = asl(v);
        m_a |= r;
        setNZ(m_a);
        break;
    case 0x20: // RLA: ROL memory, then AND
        r = rol(v);
        m_a &= r;
        setNZ(m_a);
        break;
    case 0x40: // SRE: LSR memory, then EOR
        r = lsr(v);
        m_a ^= r;
        setNZ(m_a);
        break;
    case 0x60: // RRA: ROR memory, then ADC
        r = ror(v);
        adc(r);
        break;
    case 0xC0: // DCP: DEC memory, then CMP
        r = static_cast<std::uint8_t>(v - 1);
        compare(m_a, r);
        break;
    default: // 0xE0 ISB: INC memory, then SBC
        r = static_cast<std::uint8_t>(v + 1);
        adc(static_cast<std::uint8_t>(~r));
        break;
    }
    wr(a, r);
}

void Cpu::step()
{
    if (!m_branchSkipPoll) {
        // Interrupts are effectively polled at the second-to-last cycle of
        // an instruction: an edge latched ON the final cycle defers service
        // past this boundary (05-nmi_timing, 04-nmi_control).
        if (m_nmiPending && (m_nmiStale || m_edgeCycle < m_prevInstrCycles)) {
            m_nmiPending = false;
            m_nmiStale = false;
            doInterrupt(0xFFFA, false);
            m_iPrev = true;
            return;
        }
        if (m_nmiPending) {
            m_nmiStale = true; // edge landed on the last cycle: defer
        }
    } else {
        m_branchSkipPoll = false; // taken branch ignored its last-clock poll
    }
    // The flag-change delay: CLI/SEI/PLP take effect for the poll after the
    // NEXT instruction (test 3). The decision uses the penultimate-cycle
    // sample taken while the previous instruction executed (test 4/5).
    if (m_irqPollHigh && !m_iPrev) {
        m_irqPollHigh = false;
        doInterrupt(0xFFFE, false);
        m_iPrev = true;
        return;
    }

    m_cycles = 0;
    m_instrCycle = 0;
    m_irqSampleCur = false;
    m_irqSamplePrev = false;
    m_iStart = (m_p & kI) != 0;
    if (m_trace) {
        m_trace(state());
    }

    const std::uint8_t op = fetch();

    switch (op) {
    // --- Load/store -------------------------------------------------------
    case 0xA9: m_a = fetch(); setNZ(m_a); break;
    case 0xA5: m_a = rd(fetch()); setNZ(m_a); break;
    case 0xB5: m_a = rd(zpxAddr()); setNZ(m_a); break;
    case 0xAD: m_a = rd(readPair()); setNZ(m_a); break;
    case 0xBD: m_a = readAbx(true); setNZ(m_a); break;
    case 0xB9: m_a = readAby(true); setNZ(m_a); break;
    case 0xA1: m_a = readIzx(); setNZ(m_a); break;
    case 0xB1: m_a = readIzy(true); setNZ(m_a); break;

    case 0xA2: m_x = fetch(); setNZ(m_x); break;
    case 0xA6: m_x = rd(fetch()); setNZ(m_x); break;
    case 0xB6: m_x = rd(zpyAddr()); setNZ(m_x); break;
    case 0xAE: m_x = rd(readPair()); setNZ(m_x); break;
    case 0xBE: m_x = readAby(true); setNZ(m_x); break;

    case 0xA0: m_y = fetch(); setNZ(m_y); break;
    case 0xA4: m_y = rd(fetch()); setNZ(m_y); break;
    case 0xB4: m_y = rd(zpxAddr()); setNZ(m_y); break;
    case 0xAC: m_y = rd(readPair()); setNZ(m_y); break;
    case 0xBC: m_y = readAbx(true); setNZ(m_y); break;

    case 0x85: wr(fetch(), m_a); break;
    case 0x95: wr(zpxAddr(), m_a); break;
    case 0x8D: wr(readPair(), m_a); break;
    case 0x9D: wr(addrAbxWrite(), m_a); break;
    case 0x99: wr(addrAbyWrite(), m_a); break;
    case 0x81: wr(addrIzxWrite(), m_a); break;
    case 0x91: wr(addrIzyWrite(), m_a); break;

    case 0x86: wr(fetch(), m_x); break;
    case 0x96: wr(zpyAddr(), m_x); break;
    case 0x8E: wr(readPair(), m_x); break;

    case 0x84: wr(fetch(), m_y); break;
    case 0x94: wr(zpxAddr(), m_y); break;
    case 0x8C: wr(readPair(), m_y); break;

    // --- Arithmetic / logic ------------------------------------------------
    case 0x69: adc(fetch()); break;
    case 0x65: adc(rd(fetch())); break;
    case 0x75: adc(rd(zpxAddr())); break;
    case 0x6D: adc(rd(readPair())); break;
    case 0x7D: adc(readAbx(true)); break;
    case 0x79: adc(readAby(true)); break;
    case 0x61: adc(readIzx()); break;
    case 0x71: adc(readIzy(true)); break;

    case 0xE9: case 0xEB: adc(static_cast<std::uint8_t>(~fetch())); break;
    case 0xE5: adc(static_cast<std::uint8_t>(~rd(fetch()))); break;
    case 0xF5: adc(static_cast<std::uint8_t>(~rd(zpxAddr()))); break;
    case 0xED: adc(static_cast<std::uint8_t>(~rd(readPair()))); break;
    case 0xFD: adc(static_cast<std::uint8_t>(~readAbx(true))); break;
    case 0xF9: adc(static_cast<std::uint8_t>(~readAby(true))); break;
    case 0xE1: adc(static_cast<std::uint8_t>(~readIzx())); break;
    case 0xF1: adc(static_cast<std::uint8_t>(~readIzy(true))); break;

    case 0x29: m_a &= fetch(); setNZ(m_a); break;
    case 0x25: m_a &= rd(fetch()); setNZ(m_a); break;
    case 0x35: m_a &= rd(zpxAddr()); setNZ(m_a); break;
    case 0x2D: m_a &= rd(readPair()); setNZ(m_a); break;
    case 0x3D: m_a &= readAbx(true); setNZ(m_a); break;
    case 0x39: m_a &= readAby(true); setNZ(m_a); break;
    case 0x21: m_a &= readIzx(); setNZ(m_a); break;
    case 0x31: m_a &= readIzy(true); setNZ(m_a); break;

    case 0x49: m_a ^= fetch(); setNZ(m_a); break;
    case 0x45: m_a ^= rd(fetch()); setNZ(m_a); break;
    case 0x55: m_a ^= rd(zpxAddr()); setNZ(m_a); break;
    case 0x4D: m_a ^= rd(readPair()); setNZ(m_a); break;
    case 0x5D: m_a ^= readAbx(true); setNZ(m_a); break;
    case 0x59: m_a ^= readAby(true); setNZ(m_a); break;
    case 0x41: m_a ^= readIzx(); setNZ(m_a); break;
    case 0x51: m_a ^= readIzy(true); setNZ(m_a); break;

    case 0x09: m_a |= fetch(); setNZ(m_a); break;
    case 0x05: m_a |= rd(fetch()); setNZ(m_a); break;
    case 0x15: m_a |= rd(zpxAddr()); setNZ(m_a); break;
    case 0x0D: m_a |= rd(readPair()); setNZ(m_a); break;
    case 0x1D: m_a |= readAbx(true); setNZ(m_a); break;
    case 0x19: m_a |= readAby(true); setNZ(m_a); break;
    case 0x01: m_a |= readIzx(); setNZ(m_a); break;
    case 0x11: m_a |= readIzy(true); setNZ(m_a); break;

    case 0xC9: compare(m_a, fetch()); break;
    case 0xC5: compare(m_a, rd(fetch())); break;
    case 0xD5: compare(m_a, rd(zpxAddr())); break;
    case 0xCD: compare(m_a, rd(readPair())); break;
    case 0xDD: compare(m_a, readAbx(true)); break;
    case 0xD9: compare(m_a, readAby(true)); break;
    case 0xC1: compare(m_a, readIzx()); break;
    case 0xD1: compare(m_a, readIzy(true)); break;
    case 0xE0: compare(m_x, fetch()); break;
    case 0xE4: compare(m_x, rd(fetch())); break;
    case 0xEC: compare(m_x, rd(readPair())); break;
    case 0xC0: compare(m_y, fetch()); break;
    case 0xC4: compare(m_y, rd(fetch())); break;
    case 0xCC: compare(m_y, rd(readPair())); break;

    case 0x24: case 0x2C: {
        const std::uint8_t m =
            (op == 0x24) ? rd(fetch()) : rd(readPair());
        m_p = static_cast<std::uint8_t>((m_p & ~(kZ | kN | kV))
            | (m & (kN | kV))
            | (((m_a & m) == 0) ? kZ : 0));
        break;
    }

    // --- Shifts/rotates (accumulator) --------------------------------------
    case 0x0A: tick(); m_a = asl(m_a); break;
    case 0x4A: tick(); m_a = lsr(m_a); break;
    case 0x2A: tick(); m_a = rol(m_a); break;
    case 0x6A: tick(); m_a = ror(m_a); break;

    // --- Shifts/rotates/INC/DEC (memory RMW, double write) ------------------
    case 0x06: case 0x16: case 0x0E: case 0x1E: {
        const std::uint16_t a = rmwAddr(op);
        const std::uint8_t v = rd(a);
        wr(a, v);
        wr(a, asl(v));
        break;
    }
    case 0x46: case 0x56: case 0x4E: case 0x5E: {
        const std::uint16_t a = rmwAddr(op);
        const std::uint8_t v = rd(a);
        wr(a, v);
        wr(a, lsr(v));
        break;
    }
    case 0x26: case 0x36: case 0x2E: case 0x3E: {
        const std::uint16_t a = rmwAddr(op);
        const std::uint8_t v = rd(a);
        wr(a, v);
        wr(a, rol(v));
        break;
    }
    case 0x66: case 0x76: case 0x6E: case 0x7E: {
        const std::uint16_t a = rmwAddr(op);
        const std::uint8_t v = rd(a);
        wr(a, v);
        wr(a, ror(v));
        break;
    }
    case 0xE6: case 0xF6: case 0xEE: case 0xFE: {
        const std::uint16_t a = rmwAddr(op);
        const std::uint8_t v = rd(a);
        wr(a, v);
        const std::uint8_t r = static_cast<std::uint8_t>(v + 1);
        wr(a, r);
        setNZ(r);
        break;
    }
    case 0xC6: case 0xD6: case 0xCE: case 0xDE: {
        const std::uint16_t a = rmwAddr(op);
        const std::uint8_t v = rd(a);
        wr(a, v);
        const std::uint8_t r = static_cast<std::uint8_t>(v - 1);
        wr(a, r);
        setNZ(r);
        break;
    }

    // --- Registers/flags ----------------------------------------------------
    case 0xAA: tick(); m_x = m_a; setNZ(m_x); break;
    case 0xA8: tick(); m_y = m_a; setNZ(m_y); break;
    case 0x8A: tick(); m_a = m_x; setNZ(m_a); break;
    case 0x98: tick(); m_a = m_y; setNZ(m_a); break;
    case 0xBA: tick(); m_x = m_s; setNZ(m_x); break;
    case 0x9A: tick(); m_s = m_x; break;
    case 0xE8: tick(); ++m_x; setNZ(m_x); break;
    case 0xC8: tick(); ++m_y; setNZ(m_y); break;
    case 0xCA: tick(); --m_x; setNZ(m_x); break;
    case 0x88: tick(); --m_y; setNZ(m_y); break;
    case 0x18: tick(); m_p &= static_cast<std::uint8_t>(~kC); break;
    case 0x38: tick(); m_p |= kC; break;
    case 0x58: tick(); m_p &= static_cast<std::uint8_t>(~kI); break;
    case 0x78: tick(); m_p |= kI; break;
    case 0xB8: tick(); m_p &= static_cast<std::uint8_t>(~kV); break;
    case 0xD8: tick(); m_p &= static_cast<std::uint8_t>(~kD); break;
    case 0xF8: tick(); m_p |= kD; break;

    // --- Stack ---------------------------------------------------------------
    case 0x48: tick(); push(m_a); break;
    case 0x08: tick(); push(static_cast<std::uint8_t>(m_p | kU | kB)); break;
    case 0x68: tick(); tick(); m_a = pop(); setNZ(m_a); break;
    case 0x28: // PLP: B does not exist in the register (research.md D4)
        tick();
        tick();
        m_p = static_cast<std::uint8_t>((pop() | kU) & ~kB);
        break;

    // --- Jumps/subroutines ----------------------------------------------------
    case 0x4C: m_pc = readPair(); break;
    case 0x6C: {
        const std::uint16_t ptr = readPair();
        // NMOS page-wrap bug: the high byte comes from ptr & 0xFF00.
        const std::uint8_t lo = rd(ptr);
        const std::uint8_t hi = rd(static_cast<std::uint16_t>(
            (ptr & 0xFF00) | ((ptr + 1) & 0xFF)));
        m_pc = static_cast<std::uint16_t>(lo | (hi << 8));
        break;
    }
    case 0x20: { // JSR
        const std::uint8_t lo = fetch();
        tick();
        push(static_cast<std::uint8_t>(m_pc >> 8));
        push(static_cast<std::uint8_t>(m_pc & 0xFF));
        const std::uint8_t hi = fetch();
        m_pc = static_cast<std::uint16_t>(lo | (hi << 8));
        break;
    }
    case 0x60: { // RTS
        tick();
        tick();
        const std::uint8_t lo = pop();
        const std::uint8_t hi = pop();
        tick();
        m_pc = static_cast<std::uint16_t>((lo | (hi << 8)) + 1);
        break;
    }
    case 0x40: { // RTI: B stripped like PLP
        tick();
        tick();
        m_p = static_cast<std::uint8_t>((pop() | kU) & ~kB);
        const std::uint8_t lo = pop();
        const std::uint8_t hi = pop();
        m_pc = static_cast<std::uint16_t>(lo | (hi << 8));
        break;
    }
    case 0x00: // BRK (pushes PC+2 via the signature fetch)
        doInterrupt(0xFFFE, true);
        break;

    // --- Branches --------------------------------------------------------------
    case 0x10: branch((m_p & kN) == 0); break;
    case 0x30: branch((m_p & kN) != 0); break;
    case 0x50: branch((m_p & kV) == 0); break;
    case 0x70: branch((m_p & kV) != 0); break;
    case 0x90: branch((m_p & kC) == 0); break;
    case 0xB0: branch((m_p & kC) != 0); break;
    case 0xD0: branch((m_p & kZ) == 0); break;
    case 0xF0: branch((m_p & kZ) != 0); break;

    // --- NOPs (documented + unofficial) ------------------------------------------
    case 0xEA: case 0x1A: case 0x3A: case 0x5A: case 0x7A: case 0xDA:
    case 0xFA:
        tick();
        break;
    case 0x80: case 0x82: case 0x89: case 0xC2: case 0xE2:
        (void)fetch();
        break;
    case 0x04: case 0x44: case 0x64:
        (void)rd(fetch());
        break;
    case 0x14: case 0x34: case 0x54: case 0x74: case 0xD4: case 0xF4:
        (void)rd(zpxAddr()); // zpxAddr includes the internal index cycle
        break;
    case 0x0C:
        (void)rd(readPair());
        break;
    case 0x1C: case 0x3C: case 0x5C: case 0x7C: case 0xDC: case 0xFC:
        (void)readAbx(true);
        break;

    // --- Unofficial: LAX / SAX / SBX / XAA / ANC / ALR / ARR ------------------
    case 0xA7: case 0xB7: case 0xAF: case 0xBF: case 0xA3: case 0xB3:
    case 0xAB: {
        std::uint8_t v = 0;
        switch (op) {
        case 0xA7: v = rd(fetch()); break;
        case 0xB7: v = rd(zpyAddr()); break;
        case 0xAF: v = rd(readPair()); break;
        case 0xBF: v = readAby(true); break;
        case 0xA3: v = readIzx(); break;
        case 0xB3: v = readIzy(true); break;
        case 0xAB: v = fetch(); break;
        }
        m_a = v;
        m_x = v;
        setNZ(v);
        break;
    }
    case 0x87: case 0x97: case 0x8F: case 0x83: {
        const std::uint8_t v = static_cast<std::uint8_t>(m_a & m_x);
        switch (op) {
        case 0x87: wr(fetch(), v); break;
        case 0x97: wr(zpyAddr(), v); break;
        case 0x8F: wr(readPair(), v); break;
        case 0x83: wr(addrIzxWrite(), v); break;
        }
        break;
    }
    case 0xCB: { // SBX: X = (A & X) - imm
        const std::uint8_t imm = fetch();
        const std::uint8_t t = static_cast<std::uint8_t>(m_a & m_x);
        m_p = static_cast<std::uint8_t>((m_p & ~kC) | ((t >= imm) ? kC : 0));
        m_x = static_cast<std::uint8_t>(t - imm);
        setNZ(m_x);
        break;
    }
    case 0x8B: // XAA (unstable): A = X & imm
        m_a = static_cast<std::uint8_t>(m_x & fetch());
        setNZ(m_a);
        break;
    case 0x0B: case 0x2B: // ANC
        m_a &= fetch();
        setNZ(m_a);
        m_p = static_cast<std::uint8_t>((m_p & ~kC) | ((m_a >> 7) & 1));
        break;
    case 0x4B: // ALR
        m_a &= fetch();
        m_a = lsr(m_a);
        break;
    case 0x6B: { // ARR: AND #imm then ROR, but C/V come from the AND result:
        // C = bit 7 of (A & imm) [i.e. bit 6 of the final value];
        // V = bit 6 ^ bit 5 of the final value (nesdev unofficial opcodes).
        const std::uint8_t anded = static_cast<std::uint8_t>(
            m_a & fetch());
        const std::uint8_t carryIn = static_cast<std::uint8_t>(m_p & kC);
        m_a = static_cast<std::uint8_t>((anded >> 1) | (carryIn << 7));
        m_p = static_cast<std::uint8_t>((m_p & ~(kC | kV))
            | ((anded >> 7) & 1)
            | ((((m_a >> 6) ^ (m_a >> 5)) & 1) << 6));
        setNZ(m_a);
        break;
    }

    // --- Unstable stores --------------------------------------------------------
    case 0x93: case 0x9F: { // SHA: A&X&(H+1); cross: high byte AND A&X
        const std::uint16_t a0 = op == 0x93 ? addrIzyWrite() : addrAbyWrite();
        const std::uint8_t h1 = static_cast<std::uint8_t>((a0 >> 8) + 1);
        const std::uint8_t ax = static_cast<std::uint8_t>(m_a & m_x);
        const std::uint8_t value = static_cast<std::uint8_t>(ax & h1);
        const bool crossed =
            ((a0 ^ (a0 + (op == 0x93 ? m_y : 0))) & 0xFF00) != 0;
        // addrIzyWrite/addrAbyWrite already resolved the full effective
        // address; only the page-cross corruption differs from plain stores.
        const std::uint16_t ea = crossed
            ? static_cast<std::uint16_t>(
                  (static_cast<std::uint16_t>(h1 & ax) << 8) | (a0 & 0xFF))
            : a0;
        wr(ea, value);
        break;
    }
    case 0x9B: { // SHS/TAS: S = A&X, then SHA-like store
        const std::uint16_t a0 = addrAbyWrite();
        m_s = static_cast<std::uint8_t>(m_a & m_x);
        const std::uint8_t h1 = static_cast<std::uint8_t>((a0 >> 8) + 1);
        wr(a0, static_cast<std::uint8_t>(m_s & h1));
        break;
    }
    case 0x9C: { // SHY abs,X (nesdev: value always Y&(H+1); on cross the
        // computed high byte is ANDed with Y)
        const std::uint16_t lo = fetch();
        const std::uint16_t hi = fetch();
        const std::uint16_t base = static_cast<std::uint16_t>(lo | (hi << 8));
        (void)rd(static_cast<std::uint16_t>(
            (base & 0xFF00) | ((lo + m_x) & 0xFF)));
        const std::uint8_t h1 = static_cast<std::uint8_t>((base >> 8) + 1);
        const std::uint8_t value = static_cast<std::uint8_t>(m_y & h1);
        const std::uint16_t ea = (((base ^ (base + m_x)) & 0xFF00) != 0)
            ? static_cast<std::uint16_t>(
                  (static_cast<std::uint16_t>(h1 & m_y) << 8)
                  | ((lo + m_x) & 0xFF))
            : static_cast<std::uint16_t>(base + m_x);
        wr(ea, value);
        break;
    }
    case 0x9E: { // SHX abs,Y: mirror of SHY with X/Y swapped
        const std::uint16_t lo = fetch();
        const std::uint16_t hi = fetch();
        const std::uint16_t base = static_cast<std::uint16_t>(lo | (hi << 8));
        (void)rd(static_cast<std::uint16_t>(
            (base & 0xFF00) | ((lo + m_y) & 0xFF)));
        const std::uint8_t h1 = static_cast<std::uint8_t>((base >> 8) + 1);
        const std::uint8_t value = static_cast<std::uint8_t>(m_x & h1);
        const std::uint16_t ea = (((base ^ (base + m_y)) & 0xFF00) != 0)
            ? static_cast<std::uint16_t>(
                  (static_cast<std::uint16_t>(h1 & m_x) << 8)
                  | ((lo + m_y) & 0xFF))
            : static_cast<std::uint16_t>(base + m_y);
        wr(ea, value);
        break;
    }
    case 0xBB: { // LAS
        const std::uint8_t v = readAby(true);
        m_a = m_x = m_s = static_cast<std::uint8_t>(m_s & v);
        setNZ(m_a);
        break;
    }

    case 0x02: case 0x12: case 0x22: case 0x32: case 0x42: case 0x52:
    case 0x62: case 0x72: case 0x92: case 0xB2: case 0xD2: case 0xF2:
        // KIL/JAM: the CPU halts. Keep the clock alive so the machine stays
        // inspectable; PC stays on the jam opcode.
        tick();
        m_jammed = true;
        break;

    default:
        // Everything remaining is an RMW combo (SLO/RLA/SRE/RRA/DCP/ISB)
        // in one of the seven addressing modes.
        execRmwCombo(op);
        break;
    }
    m_prevInstrCycles = m_cycles;
    // Latch the IRQ poll for the next boundary: the line as sampled at the
    // start of this instruction's penultimate cycle.
    m_irqPollHigh = (m_instrCycle >= 2) ? m_irqSamplePrev : m_irqSampleCur;
    // CLI/SEI/PLP take effect on the final cycle, so the poll sees the OLD
    // value; RTI pulls P early enough that the poll sees the NEW one
    // (cpu_interrupts_v2 tests 3, 7, and 10).
    m_iPrev = (op == 0x58 || op == 0x78 || op == 0x28) ? m_iStart
        : ((m_p & kI) != 0);
}


void Cpu::saveState(StateWriter& w) const
{
    w.begin("CPU ");
    w.w(m_a);
    w.w(m_x);
    w.w(m_y);
    w.w(m_s);
    w.w(m_p);
    w.w(m_pc);
    w.w(m_nmiPending);
    w.w(m_irqLevel);
    w.w(m_jammed);
    w.w(m_nmiWasHigh);
    w.w(m_nmiStale);
    w.w(m_iPrev);
    w.w(m_iStart);
    w.w(m_branchSkipPoll);
    w.w<std::int32_t>(m_cycles);
    w.w<std::int32_t>(m_prevInstrCycles);
    w.w<std::int32_t>(m_edgeCycle);
    w.w<std::int32_t>(m_irqEdgeCycle);
    w.w(m_irqWasHigh);
    w.w(m_irqPollHigh);
    w.w(m_irqSampleCur);
    w.w(m_irqSamplePrev);
    w.w<std::int32_t>(m_instrCycle);
    w.end();
}

void Cpu::loadState(StateReader& r)
{
    m_a = r.get<std::uint8_t>();
    m_x = r.get<std::uint8_t>();
    m_y = r.get<std::uint8_t>();
    m_s = r.get<std::uint8_t>();
    m_p = r.get<std::uint8_t>();
    m_pc = r.get<std::uint16_t>();
    m_nmiPending = r.get<std::uint8_t>() != 0;
    m_irqLevel = r.get<std::uint8_t>() != 0;
    m_jammed = r.get<std::uint8_t>() != 0;
    m_nmiWasHigh = r.get<std::uint8_t>() != 0;
    m_nmiStale = r.get<std::uint8_t>() != 0;
    m_iPrev = r.get<std::uint8_t>() != 0;
    m_iStart = r.get<std::uint8_t>() != 0;
    m_branchSkipPoll = r.get<std::uint8_t>() != 0;
    m_cycles = r.get<std::int32_t>();
    m_prevInstrCycles = r.get<std::int32_t>();
    m_edgeCycle = r.get<std::int32_t>();
    m_irqEdgeCycle = r.get<std::int32_t>();
    m_irqWasHigh = r.get<std::uint8_t>() != 0;
    m_irqPollHigh = r.get<std::uint8_t>() != 0;
    m_irqSampleCur = r.get<std::uint8_t>() != 0;
    m_irqSamplePrev = r.get<std::uint8_t>() != 0;
    m_instrCycle = r.get<std::int32_t>();
}

} // namespace cutenes
