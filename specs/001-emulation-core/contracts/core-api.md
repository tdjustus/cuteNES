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

## Save states (FR-011) and SRAM (FR-006 battery)

```cpp
struct SaveStateBlob { const std::uint8_t* data; std::size_t size; };
// Blob is valid until the next core call; consumers copy to keep.

SaveStateBlob Machine::captureState();        // any instant, mid-frame included
LoadStateError Machine::loadState(const std::uint8_t* data, std::size_t size);
// LoadStateError: Ok | BadMagic | IncompatibleFormat | IncompatibleCoreVersion
//                | WrongCartridge | ChecksumMismatch

SramRange Machine::sram() const;              // const view when battery
void      Machine::loadSram(const std::uint8_t* data, std::size_t size); // pad/trim per header
```

Blob layout: [save-state-format.md](save-state-format.md). Mismatch ⇒ reject,
never guess (research D9).

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
| Run/pause/frame/run-N (FR-008) | `runFrames` `frameAdvance` `pause` `resume` |
| Single-step (FR-008) | `stepInstruction` |
| Power/reset (FR-008) | `powerOn` `reset` |
| Input inject (FR-007) | `setController` |
| Save/load/list states (FR-011) | `captureState` / `loadState` (+ slot mgmt is consumer-side naming) |
| Memory r/w (FR-016) | `readMemory` `writeMemory` `writeCpuRegister` |
| CPU state (FR-016) | `cpu()` |
| Screen capture (FR-009) | `IVideoSink::onFrame` (consumer retains) |
| Audio (FR-010) | `IAudioSink::onSamples` |
| Region detect (FR-014) | `region()` |
| SRAM persistence (FR-006) | `sram()` / `loadSram` |
