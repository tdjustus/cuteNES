# Contract: Core Public API (`cutenes-core`)

**Feature**: 001-emulation-core | **Authority**: spec FR-001..FR-016

The public C++20 API of the `cutenes-core` library. This is the interface the
Qt frontend (future `003`) and MCP server (`002`) consume; per constitution
Principle II it contains no Qt, GUI, or protocol types. Signatures below are
the contract — names are stable through 1.0; layout of argument structs may
grow by appending fields only.

## Global rules

- **Namespace**: `cutenes`. All types below are `cutenes::`-qualified implicitly.
- **No exceptions cross the API boundary.** All fallible operations return
  result/error-code types. The core does not throw.
- **Single-threaded.** The core spawns no threads, reads no wall clock, and
  performs no I/O. All state is instance state (see data-model integrity
  rules). Consumers may call from one thread at a time.
  - **One exception: `dumpMemory`.** It reads the clock for its file name and
    writes the file. It runs only when called, changes no machine state, and
    has a pure counterpart, `memoryDump`, for consumers that must not touch
    the filesystem (#68).
- **Determinism.** Identical (ROM, operation sequence, input sequence,
  starting state) ⇒ byte-identical sinks output and identical observable
  state (FR-012).
- **Header-only entry point**: `#include <cutenes/cutenes.hpp>` pulls the
  whole public API.

## Cartridge loading

```cpp
enum class LoadError : std::uint8_t {
    Ok, NotAnInesFile, TruncatedFile, InvalidHeader,
    InvalidBankCounts, UnsupportedMapper, Nes2FieldInvalid,
};

struct LoadResult {                 // Ok ⇒ error == LoadError::Ok
    LoadError error;                // exact defect, per FR-001
    std::uint16_t detail;           // e.g. unsupported mapper id, bad byte offset
    CartridgeInfo info;             // valid when error == Ok
};

struct CartridgeInfo {
    std::uint8_t  formatVersion;    // 0 = iNES 1.0, 2 = NES 2.0
    std::uint16_t mapperId;         // 0,1,2,3,4 supported initially
    Mirroring     mirroring;        // Horizontal|Vertical|FourScreen|MapperControlled
    bool          battery;
    std::size_t   prgSize, chrSize; // chrSize == 0 ⇒ CHR-RAM
    std::size_t   prgRamSize;
    Region        region;           // NTSC|PAL|Unspecified (header-derived)
};

LoadResult Machine::loadCartridge(const std::uint8_t* data, std::size_t size);

// MMC3 revision selection (mapper 4 IRQ behavior). Auto: revision B, or A
// when the NES 2.0 header sets submapper 4; explicit values override the
// header. Applies to the loaded cartridge and to subsequent loads.
enum class Mmc3Revision { Auto, B, A };
void Machine::setMmc3Revision(Mmc3Revision revision);
Mmc3Revision Machine::mmc3Revision() const;
void       Machine::ejectCartridge();
```

An `UnsupportedMapper` result names the mapper id in `detail` (spec scenario:
never silently mis-execute).

## Run control (FR-008)

```cpp
void Machine::powerOn();               // full cold boot (P=$24, S=$FD, 7-cycle reset)
void Machine::reset();                 // console reset-button behavior
void Machine::runFrames(std::uint32_t n);   // blocking; delivers n video frames + audio
void Machine::frameAdvance();          // exactly one frame
void Machine::stepInstruction();       // exactly one CPU instruction (dependent
                                       // components advance to the next boundary)
void Machine::pause();
void Machine::resume();
RunState Machine::runState() const;    // NeedsRom | Paused | Running
```

`stepInstruction` leaves the machine inspectable at an instruction boundary
(FR-016); `runFrames`/`frameAdvance` sink the frames they produce.

**Pause semantics**: `pause`/`resume` govern *autonomous* pacing (the GUI's
real-time loop). The explicit advance commands — `runFrames`,
`frameAdvance`, `stepInstruction` — execute regardless of `runState`;
they are commands, not a request to unpause.

**Scripted input** (002 FR-019):

```cpp
struct InputScript {              // per-frame controller plan
    const ButtonSet* frames;      // frames[i] applies before frame i
    std::size_t        count;     // past the end: hold the last entry
};
void Machine::runFrames(std::uint32_t n, const InputScript* script);
```

Scripted runs MUST be byte-identical to equivalent per-frame
`setController` + `runFrames(1)` calls.

## Input (FR-007)

```cpp
enum class Port : std::uint8_t { One = 1, Two = 2 };
enum class Button : std::uint8_t { Right, Left, Down, Up, Start, Select, B, A };
using ButtonSet = /* std::bitset<8> over Button, serial-shift order above */;

void Machine::setController(Port p, ButtonSet buttons);  // full-replace, any time
ButtonSet Machine::controller(Port p) const;
```

The machine feeds these through authentic serial polling; games cannot
distinguish injected from physical input (spec US4).

## Media sinks (FR-009, FR-010)

```cpp
struct IVideoSink {                       // implemented by consumers
    virtual void onFrame(const VideoFrame& frame) = 0;   // once per frame
protected: ~IVideoSink() = default;
};
struct IAudioSink {
    virtual void onSamples(const std::int16_t* pcm, std::size_t count,
                           std::uint64_t startMasterCycle) = 0;
protected: ~IAudioSink() = default;
};
void Machine::setVideoSink(IVideoSink*);
void Machine::setAudioSink(IAudioSink*);
```

Formats are fixed in [media-output.md](media-output.md). Sinks are called
synchronously during `run*`/`step*`; bounded work only — the core never
blocks on a slow sink (audio delivery is chunked, buffers bounded).

## Observation and modification (FR-016, spec US4)

```cpp
struct CpuState { std::uint8_t a, x, y, s, p /* bit5=1, no B flag */;
                  std::uint16_t pc; std::uint64_t masterCycle; };

CpuState                Machine::cpu() const;
void                    Machine::readMemory(std::uint16_t addr,
                                            std::size_t len,
                                            std::uint8_t* dst) const;
WriteError              Machine::writeMemory(std::uint16_t addr,
                                             const std::uint8_t* src,
                                             std::size_t len);
WriteError              Machine::writeCpuRegister(std::uint16_t addr,
                                                  std::uint8_t value);
```

`readMemory` observes CPU-bus view incl. register side effects of *reads*
(e.g., $2002 clears VBlank). Writes through `writeMemory` are the automation
surface (cheats/trainer logic); bounded per call — large accesses chunk.

### CPU state write (002 FR-002: full control)

```cpp
struct CpuWrite {                    // all fields optional via mask
    std::uint8_t  a, x, y, s, p;     // p bit 5 ignored (always reads 1)
    std::uint16_t pc;
    std::uint8_t  mask;              // bit i set ⇒ field written
};                                    // bits: 0=a 1=x 2=y 3=s 4=p 5=pc

void Machine::writeCpu(const CpuWrite& w);
```

Applies immediately (components caught up to the current clock); interrupt
recognition is re-evaluated at the next instruction boundary. Determinism
is preserved: identical write + operation sequences produce identical
output.

### Side-effect-free debug access (002 FR-015)

```cpp
void Machine::readPpuRegisters(std::uint8_t dst[8]) const; // no latch side effects
void Machine::readVram(std::uint16_t addr, std::size_t len,
                       std::uint8_t* dst) const;   // PPU-bus view, mirroring resolved
void Machine::writeVram(std::uint16_t addr, const std::uint8_t* src,
                        std::size_t len);
void Machine::readOam(std::uint8_t dst[256]) const;
void Machine::writeOam(const std::uint8_t src[256]);
void Machine::readPalette(std::uint8_t dst[32]) const;    // $3F00-$3F1F, aliases resolved
void Machine::writePalette(const std::uint8_t src[32]);

std::uint8_t Machine::peek(std::uint16_t addr) const;     // side-effect-free CPU-bus read
std::string  Machine::disassemble(std::uint16_t pc) const; // next instruction, no execution
```

These exist so debugger-grade clients can inspect and repair video state
without the destructive `$2006/$2007` register dance, and so disassembly
never perturbs the machine. `peek`/`disassemble` promote the existing
harness surface to the stable API (002 FR-002).

## Save states (FR-011) and SRAM (FR-006 battery)

```cpp
struct SaveStateBlob { const std::uint8_t* data; std::size_t size; };
// Blob is valid until the next core call; consumers copy to keep.

SaveStateBlob Machine::captureState();        // any instant, mid-frame included
LoadStateError Machine::loadState(const std::uint8_t* data, std::size_t size);
// LoadStateError: Ok | BadMagic | IncompatibleFormat | IncompatibleCoreVersion
//                | WrongCartridge | ChecksumMismatch

SramRange Machine::sram() const;              // const view when battery; empty otherwise (#71)
void      Machine::loadSram(const std::uint8_t* data, std::size_t size); // pad/trim per header
```

Blob layout: [save-state-format.md](save-state-format.md). Mismatch ⇒ reject,
never guess (research D9).

## Memory dump (debugging)

```cpp
std::vector<std::uint8_t> Machine::memoryDump(std::uint64_t timestamp = 0) const;
std::string Machine::dumpMemory(const std::string& debugDir = "debug") const;
```

`memoryDump` returns the dump bytes. It reads every section side-effect free
and does no I/O. `dumpMemory` writes `memoryDump(now)` to
`<debugDir>/<YYYYmmddHHMMSS>.dmp` and returns the path. Layout (integers
little-endian):

| Offset | Size | Content |
|---|---|---|
| 0 | 8 | `CUNEDMP1` |
| 8 | 8 | timestamp (the caller's; `dumpMemory` uses Unix seconds) |
| 16 | 8 | `frameCounter()` |
| 24 | 8 | `masterCycle()` |
| 32 | 65536 | CPU bus `$0000-$FFFF` via `peek` |
| +0 | 8 | `readPpuRegisters` |
| +8 | 256 | `readOam` |
| +264 | 32 | `readPalette` |
| +296 | 4 | PRG-RAM size N |
| +300 | N | PRG-RAM contents |
| +300+N | 4 | `VRAM` |
| +304+N | 4 | 16384 |
| +308+N | 16384 | PPU bus `$0000-$3FFF` via `readVram` (pattern tables, nametables, palette) |

## Region and counters

```cpp
Region        Machine::region() const;        // resolved at load; NTSC default
std::uint64_t Machine::frameCounter() const;
std::uint64_t Machine::masterCycle() const;   // the sole time source
```

## Configuration

```cpp
struct MachineConfig {
    std::uint32_t audioSampleRate = 48000;    // 44100|48000|96000
    // appended-fields-only growth from v1.1 on
};
explicit Machine(const MachineConfig& = {});
```

## Capability completeness (automation-parity anchor)

Feature 002's FR-003 audit maps every row of this table to an MCP tool. This
table is therefore part of the contract:

| Capability (spec FR) | API |
|---|---|
| Load/eject ROM (FR-001) | `loadCartridge` / `ejectCartridge` |
| Cartridge info (FR-001) | `cartridgeInfo` |
| MMC3 revision (mapper 4) | `setMmc3Revision` / `mmc3Revision` |
| Run/pause/frame/run-N (FR-008) | `runFrames` `frameAdvance` `pause` `resume` |
| Single-step (FR-008) | `stepInstruction` |
| Power/reset (FR-008) | `powerOn` `reset` |
| Input inject (FR-007) | `setController` / `controller` |
| Save/load/list states (FR-011) | `captureState` / `loadState` (+ slot mgmt is consumer-side naming) |
| Memory r/w (FR-016) | `readMemory` `writeMemory` `writeCpuRegister` |
| CPU state (FR-016) | `cpu()` |
| Screen capture (FR-009) | `IVideoSink::onFrame` (consumer retains) |
| Audio (FR-010) | `IAudioSink::onSamples` |
| Region detect (FR-014) | `region()` |
| Counters | `frameCounter` / `masterCycle` |
| Run state (FR-008) | `runState` |
| SRAM persistence (FR-006) | `sram()` / `loadSram` |
| CPU state write (002 FR-002) | `writeCpu` |
| PPU debug access (002 FR-015) | `readPpuRegisters` `readVram` `writeVram` `readOam` `writeOam` `readPalette` `writePalette` |
| Side-effect-free peek (002 FR-002) | `peek` / `disassemble` |
| Scripted input runs (002 FR-019) | `runFrames(n, InputScript*)` |
| Memory dump (debugging) | `memoryDump` / `dumpMemory` |
| Instruction trace (harness/debug) | `setInstructionTrace` (002 exposes it as a bounded capture) |
| Set PC (harness/debug) | `debugSetProgramCounter` (002 covers it with `writeCpu`) |
| Status/info (002 FR-018) | `cartridgeInfo` `region` `frameCounter` `masterCycle` `runState` + `coreVersionString` |

### Debug memory dump (debugging)

```cpp
// Creates a debug directory if missing and writes a binary memory dump
// with timestamp in filename: <YYYYMMDDHHMMSS>.dmp (or ISO8601)
std::string Machine::dumpMemory(const std::string& debugDir = "debug") const;
// Returns absolute/relative path to the written dump file.
// Dump includes CPU memory, PPU memory, OAM, palette, and cartridge RAM/ROM snapshots.
```
