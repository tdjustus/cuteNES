#pragma once

#include <cutenes/cartridge.hpp>
#include <cutenes/controller.hpp>
#include <cutenes/media.hpp>
#include <cutenes/save_state.hpp>
#include <cutenes/state_view.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace cutenes {

enum class RunState : std::uint8_t { NeedsRom, Paused, Running };

// Masked CPU register write (002 full control): bit i set => field written.
struct CpuWrite {
    std::uint8_t  a = 0, x = 0, y = 0, s = 0, p = 0;
    std::uint16_t pc = 0;
    std::uint8_t  mask = 0;          // bits: 0=a 1=x 2=y 3=s 4=p 5=pc
};

// Per-frame controller plan (002 FR-019): frames[i] applies before frame i;
// past the end the last entry is held.
struct InputScript {
    const ButtonSet* frames = nullptr;
    std::size_t      count = 0;
};

struct MachineConfig {
    // 44100 | 48000 | 96000; growth by appended fields only from v1.1 on
    // (contracts/core-api.md).
    std::uint32_t audioSampleRate = 48000;
};

// The single emulation session — aggregate root of the core (data-model.md).
// Exception-free, single-threaded, no I/O, no wall-clock reads; identical
// operation sequences on identical state produce byte-identical output
// (spec FR-012).
class Machine {
public:
    explicit Machine(const MachineConfig& config = {});
    ~Machine();

    Machine(Machine&&) noexcept;
    Machine& operator=(Machine&&) noexcept;

    Machine(const Machine&) = delete;
    Machine& operator=(const Machine&) = delete;

    // --- Cartridge (FR-001) ---------------------------------------------
    LoadResult loadCartridge(const std::uint8_t* data, std::size_t size);
    void ejectCartridge();
    const CartridgeInfo& cartridgeInfo() const;

    // --- MMC3 revision (mapper 4 IRQ behavior) -----------------------------
    // Auto (default): revision B, or A when the NES 2.0 header sets mapper 4
    // submapper 4. Explicit A/B override the header. Applies to the loaded
    // cartridge and to subsequent loads.
    enum class Mmc3Revision { Auto, B, A };
    void setMmc3Revision(Mmc3Revision revision);
    Mmc3Revision mmc3Revision() const;

    // --- Run control (FR-008) -------------------------------------------
    void powerOn();                          // cold boot: P=$24, S=$FD,
                                             // 7-cycle reset (research D4)
    void reset();                            // reset-button behavior
    void runFrames(std::uint32_t frames);    // blocking; sinks n frames
    void frameAdvance();                     // exactly one frame
    void stepInstruction();                  // one CPU instruction; dependent
                                             // components advance to the
                                             // next instruction boundary
    void pause();
    void resume();
    RunState runState() const;

    // --- Input (FR-007) --------------------------------------------------
    void setController(Port port, ButtonSet buttons); // full replace
    ButtonSet controller(Port port) const;

    // --- Media sinks (FR-009, FR-010) -------------------------------------
    void setVideoSink(IVideoSink* sink);
    void setAudioSink(IAudioSink* sink);

    // --- Full-control surface (002 review amendment) -----------------------
    void writeCpu(const CpuWrite& w);    // p bit 5 forced, B never materialized
    void runFrames(std::uint32_t frames, const InputScript* script);

    // Side-effect-free PPU debug access (FR-015).
    void readPpuRegisters(std::uint8_t dst[8]) const; // reconstructed views
    void readVram(std::uint16_t addr, std::size_t len, std::uint8_t* dst) const;
    void writeVram(std::uint16_t addr, const std::uint8_t* src,
                   std::size_t len);
    void readOam(std::uint8_t dst[256]) const;
    void writeOam(const std::uint8_t src[256]);
    void readPalette(std::uint8_t dst[32]) const; // $3F00-$3F1F, aliases resolved
    void writePalette(const std::uint8_t src[32]);

    std::uint8_t peek(std::uint16_t addr) const;  // side-effect-free bus read
    std::string  disassemble(std::uint16_t pc) const; // trace-format, no exec

    // --- Observation & modification (FR-016) ------------------------------
    CpuState cpu() const;
    void readMemory(std::uint16_t addr, std::size_t len,
                    std::uint8_t* dst) const;
    WriteError writeMemory(std::uint16_t addr, const std::uint8_t* src,
                           std::size_t len);
    WriteError writeCpuRegister(std::uint16_t addr, std::uint8_t value);

    // --- Save states (FR-011) and SRAM (FR-006 battery) -------------------
    SaveStateBlob captureState(); // any instant, mid-frame included
    LoadStateError loadState(const std::uint8_t* data, std::size_t size);
    SramRange sram() const;       // empty view when cartridge has no battery
    void loadSram(const std::uint8_t* data, std::size_t size); // pad/trim

    // --- Harness/debug surface (not part of the stable API) ----------------
    void setInstructionTrace(
        std::function<void(const TracePoint&)> callback); // per instruction
    void debugSetProgramCounter(std::uint16_t pc); // nestest entry $C000

    // --- Debugging ---------------------------------------------------------
    std::string dumpMemory(const std::string& debugDir = "debug") const;

    // --- Region & counters -------------------------------------------------
    Region region() const;
    std::uint64_t frameCounter() const;
    std::uint64_t masterCycle() const; // the sole time source

private:
    void applyMmc3Revision(); // resolve Auto/override onto the mapper
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace cutenes
