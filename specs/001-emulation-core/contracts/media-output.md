# Contract: Media Output Formats

**Feature**: 001-emulation-core | **Authority**: FR-009, FR-010, FR-012

Deterministic, byte-exact definitions of what the core delivers to sinks.
These formats are frozen: hashing them is the determinism oracle (spec
SC-002, FR-012).

## Video frames

- **Dimensions**: 256 x 240 pixels, both regions (PAL renders the same
  256x240 raster; only timing differs).
- **Pixel format**: 32 bits per pixel, `XRGB8888` — one `std::uint32_t` per
  pixel, little-endian, bits 0-7 blue, 8-15 green, 16-23 red, 24-31 unused
  (zero). Row-major, stride = 256 `uint32_t` (no padding), 61440 bytes/frame.
- **Palette**: the core maps NES palette indices through a built-in default
  palette table (the table is a versioned part of this contract — changing
  it is a breaking change). Consumers may post-process; the sink always
  receives the default-palette mapping so hashes are stable.
- **Delivery boundary**: a frame is delivered when the final visible pixel
  (scanline 239, dot 255) is emitted; the sink call happens within the same
  `run*` call, exactly once per completed frame.
- **Frame identity**: `VideoFrame { const std::uint32_t* pixels;
  std::uint64_t frameIndex; }` — `frameIndex` equals `Machine::frameCounter()`.

## Audio stream

- **Format**: mono, signed 16-bit PCM, little-endian.
- **Output rate**: the configured `MachineConfig::audioSampleRate`
  (44100 / 48000 / 96000), constant per machine. Default 48000.
- **Generation**: one mixed sample per APU cycle at the native rate
  (CPU/2), down-converted in-core by the deterministic fixed-point
  band-limited synthesizer (research D8). Identical emulated input ⇒
  identical int16 sequence on every platform and compiler.
- **Delivery**: chunked via `IAudioSink::onSamples(pcm, count,
  startMasterCycle)` as emulated time advances; `startMasterCycle` ties each
  chunk to the master clock (audio is frame-rate-locked, never wall-clock).
  Chunks are bounded (<= 4096 samples); count varies — consumers must not
  assume per-frame constants (the NES frame is not exactly 1/60 s).
- **No drift compensation in-core.** Host clock synchronization (dynamic
  rate adjustment, buffering) is consumer-side and disabled by default
  during any verification run.

## Hashing / verification conventions

Used by the test harness and quickstart scenarios:

- **Video hash**: FNV-1a 32 (or stronger, consumer choice) over the 61440
  raw frame bytes, in delivery order.
- **Audio hash**: over the concatenated int16 stream from `powerOn` to a
  given master cycle, in delivery order.
- **Golden comparisons** always compare (video-hash sequence, audio bytes,
  CPU-visible state at boundary) between two runs or run/restore pairs.
