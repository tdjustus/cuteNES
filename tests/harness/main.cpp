// rom_harness — headless verification driver for cutenes-core
// (specs/001-emulation-core/quickstart.md scenarios).
//
// Implemented: run (T012 + integration), determinism (T027).
// Pending: suite T031, nestest T029, savestate T051, playtest T059, bench T061.

#include <cutenes/cutenes.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace cutenes {
class NestestRunner {
public:
    static int run(const char* romPath, const char* logPath, bool printLines);
};
} // namespace cutenes


namespace {

std::uint32_t fnv1a32(const std::uint8_t* data, std::size_t size)
{
    std::uint32_t hash = 0x811C9DC5u;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 0x01000193u;
    }
    return hash;
}

bool readRom(const char* path, std::vector<std::uint8_t>& out)
{
    std::FILE* f = std::fopen(path, "rb");
    if (f == nullptr) {
        std::fprintf(stderr, "rom_harness: cannot open %s\n", path);
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(static_cast<std::size_t>(size));
    const std::size_t got =
        std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    return got == out.size();
}

class RecordingSink final : public cutenes::IVideoSink,
                            public cutenes::IAudioSink {
public:
    void onFrame(const cutenes::VideoFrame& frame) override
    {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(frame.pixels);
        hashes.push_back(fnv1a32(bytes, 256 * 240 * 4)); // 61440 bytes
        if (keepLast) {
            last.assign(frame.pixels, frame.pixels + 256 * 240);
        }
    }

    void onSamples(const std::int16_t* pcm, std::size_t count,
        std::uint64_t) override
    {
        audio.insert(audio.end(), pcm, pcm + count);
    }

    bool keepLast = false;
    std::vector<std::uint32_t> last; // XRGB8888, only when keepLast
    std::vector<std::uint32_t> hashes;
    std::vector<std::int16_t> audio;
};

bool writePpm(const char* path, const std::vector<std::uint32_t>& xrgb)
{
    std::FILE* f = std::fopen(path, "wb");
    if (f == nullptr) {
        return false;
    }
    std::fprintf(f, "P6\n256 240\n255\n");
    for (const std::uint32_t px : xrgb) {
        const std::uint8_t rgb[3] = {static_cast<std::uint8_t>(px >> 16),
            static_cast<std::uint8_t>(px >> 8), static_cast<std::uint8_t>(px)};
        std::fwrite(rgb, 1, 3, f);
    }
    std::fclose(f);
    return true;
}

int cmdRun(const char* romPath, std::uint32_t frames)
{
    std::vector<std::uint8_t> rom;
    if (!readRom(romPath, rom)) {
        return 1;
    }
    cutenes::Machine machine;
    const auto result =
        machine.loadCartridge(rom.data(), rom.size());
    if (result.error != cutenes::LoadError::Ok) {
        std::fprintf(stderr, "rom_harness: load error %d detail %u\n",
            static_cast<int>(result.error), result.detail);
        return 1;
    }
    RecordingSink sink;
    machine.setVideoSink(&sink);
    machine.setAudioSink(&sink);
    machine.powerOn();
    machine.runFrames(frames);

    std::printf("region %s; ", machine.region() == cutenes::Region::Pal ? "PAL" : "NTSC");
    std::printf("ran %llu frames; first %08x last %08x audio %llu samples\n",
        static_cast<unsigned long long>(sink.hashes.size()),
        sink.hashes.empty() ? 0 : sink.hashes.front(),
        sink.hashes.empty() ? 0 : sink.hashes.back(),
        static_cast<unsigned long long>(sink.audio.size()));
    return 0;
}

int cmdDeterminism(const char* romPath, std::uint32_t frames)
{
    std::vector<std::uint8_t> rom;
    if (!readRom(romPath, rom)) {
        return 1;
    }

    RecordingSink first;
    RecordingSink second;
    for (int pass = 0; pass < 2; ++pass) {
        cutenes::Machine machine;
        const auto result =
            machine.loadCartridge(rom.data(), rom.size());
        if (result.error != cutenes::LoadError::Ok) {
            std::fprintf(stderr, "rom_harness: load error %d\n",
                static_cast<int>(result.error));
            return 1;
        }
        RecordingSink& sink = pass == 0 ? first : second;
        machine.setVideoSink(&sink);
        machine.setAudioSink(&sink);
        machine.powerOn();
        machine.runFrames(frames);
    }

    if (first.hashes != second.hashes || first.audio != second.audio) {
        std::printf("determinism: FAIL (frames %zu/%zu, audio %zu/%zu)\n",
            first.hashes.size(), second.hashes.size(), first.audio.size(),
            second.audio.size());
        return 1;
    }
    std::printf("determinism: PASS (%zu frames byte-identical across runs)\n",
        first.hashes.size());
    return 0;
}

int cmdNestest(int argc, char** argv)
{
    const char* rom = "tests/roms/other/nestest.nes";
    const char* log = "tests/roms/other/nestest.log";
    bool print = false;
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--print") == 0) {
            print = true;
        } else if (std::strcmp(argv[i], "--rom") == 0 && i + 1 < argc) {
            rom = argv[++i];
        } else if (std::strcmp(argv[i], "--log") == 0 && i + 1 < argc) {
            log = argv[++i];
        }
    }
    return cutenes::NestestRunner::run(rom, log, print);
}

// blargg console-protocol driver (task T030): signature $DE $B0 $61 at
// $6001-$6003, status at $6000 ($80 running, $81 reset-request, $00-$7F
// terminal result), zero-terminated text at $6004 (research.md D11).
int cmdSuite(const char* romPath)
{
    std::vector<std::uint8_t> rom;
    if (!readRom(romPath, rom)) {
        return 2;
    }
    cutenes::Machine machine;
    const auto result = machine.loadCartridge(rom.data(), rom.size());
    if (result.error != cutenes::LoadError::Ok) {
        std::fprintf(stderr, "suite: load error %d detail %u\n",
            static_cast<int>(result.error), result.detail);
        return 2;
    }
    machine.powerOn();

    const auto readStatus = [&machine]() {
        std::uint8_t v = 0;
        machine.readMemory(0x6000, 1, &v);
        return v;
    };
    const auto signatureOk = [&machine]() {
        std::uint8_t sig[3] = {0, 0, 0};
        machine.readMemory(0x6001, 3, sig);
        return sig[0] == 0xDE && sig[1] == 0xB0 && sig[2] == 0x61;
    };
    const auto readText = [&machine](char* out, std::size_t cap) {
        std::size_t n = 0;
        for (std::uint16_t a = 0x6004; a < 0x7000 && n + 1 < cap; ++a) {
            std::uint8_t c = 0;
            machine.readMemory(a, 1, &c);
            if (c == 0) {
                break;
            }
            out[n++] = static_cast<char>(c);
        }
        out[n] = 0;
    };

    // Wait for the signature (tests initialize it after boot).
    bool signed_ = false;
    for (int frame = 0; frame < 600 && !signed_; ++frame) {
        machine.runFrames(1);
        signed_ = signatureOk();
    }
    if (!signed_) {
        std::printf("suite: %s: no $6000 protocol signature\n", romPath);
        return 3;
    }

    int status = 0x80;
    for (int frame = 0; frame < 3600; ++frame) { // ~60s emulated cap
        status = readStatus();
        if (status == 0x81) {
            // Reset request: honor with >=100 ms emulated delay.
            machine.runFrames(7);
            machine.reset();
            machine.runFrames(7);
            continue;
        }
        if (status < 0x80) {
            break; // terminal result code
        }
        machine.runFrames(1);
    }

    char text[512];
    readText(text, sizeof text);
    const bool pass = (status == 0);
    std::printf("suite: %s: %s (code %02X) %s\n", romPath,
        pass ? "PASS" : "FAIL", static_cast<unsigned>(status), text);
    return pass ? 0 : 1;
}

// T043: a frame completed entirely by single-stepping must be
// byte-identical to the same frame via runFrames (spec US3).
int cmdStepEquiv(const char* romPath, std::uint32_t frame)
{
    std::vector<std::uint8_t> rom;
    if (!readRom(romPath, rom)) {
        return 2;
    }
    RecordingSink sinkA;
    RecordingSink sinkB;
    for (int pass = 0; pass < 2; ++pass) {
        cutenes::Machine machine;
        if (machine.loadCartridge(rom.data(), rom.size()).error
            != cutenes::LoadError::Ok) {
            return 2;
        }
        RecordingSink& sink = pass == 0 ? sinkA : sinkB;
        machine.setVideoSink(&sink);
        machine.setAudioSink(&sink);
        machine.powerOn();
        for (std::uint32_t f = 0; f < frame; ++f) {
            if (pass == 0) {
                machine.runFrames(1);
            } else {
                const std::size_t before = sink.hashes.size();
                while (sink.hashes.size() == before) {
                    machine.stepInstruction();
                }
            }
        }
    }
    if (sinkA.hashes != sinkB.hashes) {
        std::printf("step-equiv: FAIL (frame %u differs)\n", frame);
        return 1;
    }
    std::printf("step-equiv: PASS (%u frames identical via stepping)\n",
        frame);
    return 0;
}

// T051: capture at a frame, rerun, restore, rerun — byte-identical.
int cmdSavestate(const char* romPath, std::uint32_t atFrame,
    std::uint32_t rerunTo)
{
    std::vector<std::uint8_t> rom;
    if (!readRom(romPath, rom)) {
        return 2;
    }
    cutenes::Machine machine;
    if (machine.loadCartridge(rom.data(), rom.size()).error
        != cutenes::LoadError::Ok) {
        return 2;
    }
    RecordingSink sink;
    machine.setVideoSink(&sink);
    machine.setAudioSink(&sink);
    machine.powerOn();

    machine.runFrames(atFrame);
    std::vector<std::uint8_t> blob;
    const cutenes::SaveStateBlob view = machine.captureState();
    blob.assign(view.data, view.data + view.size);

    // The uninterrupted reference run, and the machine state it ends in.
    const std::size_t span = rerunTo - atFrame;
    const std::size_t refHashStart = sink.hashes.size();
    const std::size_t refAudioStart = sink.audio.size();
    machine.runFrames(span);
    const std::size_t refHashEnd = sink.hashes.size();
    const std::size_t refAudioEnd = sink.audio.size();
    const cutenes::SaveStateBlob refEndView = machine.captureState();
    const std::vector<std::uint8_t> refEnd(
        refEndView.data, refEndView.data + refEndView.size);

    // Restore and rerun the same span. Video, audio and the final machine
    // state must all match the reference exactly.
    bool ok = machine.loadState(blob.data(), blob.size())
        == cutenes::LoadStateError::Ok;
    machine.runFrames(span);
    const std::vector<std::uint32_t> refHashes(sink.hashes.begin()
            + static_cast<std::ptrdiff_t>(refHashStart),
        sink.hashes.begin() + static_cast<std::ptrdiff_t>(refHashEnd));
    const std::vector<std::int16_t> refAudio(sink.audio.begin()
            + static_cast<std::ptrdiff_t>(refAudioStart),
        sink.audio.begin() + static_cast<std::ptrdiff_t>(refAudioEnd));
    const std::vector<std::uint32_t> rerunHashes(sink.hashes.begin()
            + static_cast<std::ptrdiff_t>(refHashEnd),
        sink.hashes.end());
    const std::vector<std::int16_t> rerunAudio(sink.audio.begin()
            + static_cast<std::ptrdiff_t>(refAudioEnd),
        sink.audio.end());
    const bool videoOk = refHashes.size() == span && refHashes == rerunHashes;
    const bool audioOk = !refAudio.empty() && refAudio == rerunAudio;
    const cutenes::SaveStateBlob rerunEndView = machine.captureState();
    const bool stateOk = std::vector<std::uint8_t>(rerunEndView.data,
                             rerunEndView.data + rerunEndView.size) == refEnd;
    if (!stateOk) {
        std::size_t at = 0;
        const std::size_t n = std::min(refEnd.size(), rerunEndView.size);
        while (at < n && refEnd[at] == rerunEndView.data[at]) {
            ++at;
        }
        std::printf("savestate: end states differ at byte %zu of %zu/%zu\n",
            at, refEnd.size(), rerunEndView.size);
    }
    if (!videoOk || !audioOk || !stateOk) {
        std::printf("savestate: mismatch:%s%s%s (audio %zu vs %zu samples)\n",
            videoOk ? "" : " video", audioOk ? "" : " audio",
            stateOk ? "" : " state", refAudio.size(), rerunAudio.size());
    }
    ok = ok && videoOk && audioOk && stateOk;

    // Restoring the same blob again must also be stable (repeatable).
    const std::size_t again = sink.hashes.size();
    ok = ok && machine.loadState(blob.data(), blob.size())
        == cutenes::LoadStateError::Ok;
    machine.runFrames(span);
    ok = ok
        && std::vector<std::uint32_t>(sink.hashes.begin()
               + static_cast<std::ptrdiff_t>(again), sink.hashes.end())
        == refHashes;

    std::printf("savestate: %s (%u->%u round trip: video, audio, state)\n",
        ok ? "PASS" : "FAIL", atFrame, rerunTo);
    return ok ? 0 : 1;
}

// T061: sustained emulation speed vs real-time (2x headroom = SC-003).
int cmdBench(const char* romPath, std::uint32_t seconds, double minX)
{
    std::vector<std::uint8_t> rom;
    if (!readRom(romPath, rom)) {
        return 2;
    }
    cutenes::Machine machine;
    if (machine.loadCartridge(rom.data(), rom.size()).error
        != cutenes::LoadError::Ok) {
        return 2;
    }
    RecordingSink sink;
    machine.setVideoSink(&sink);
    machine.setAudioSink(&sink);
    machine.powerOn();

    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    std::uint64_t frames = 0;
    std::uint64_t ticks0 = 0;
    while (true) {
        machine.runFrames(60);
        frames += 60;
        const double elapsed = std::chrono::duration<double>(
            Clock::now() - start).count();
        if (elapsed >= seconds) {
            ticks0 = machine.masterCycle();
            const double fps = frames / elapsed;
            std::printf("bench: %.1f fps (%.2fx real-time)\n", fps,
                fps / 60.0988);
            return fps / 60.0988 >= minX ? 0 : 1; // default 2x headroom gate
        }
    }
}

// Write the frame shown after N frames as a binary PPM (for inspection).
int cmdShot(const char* romPath, std::uint32_t frames, const char* outPath)
{
    std::vector<std::uint8_t> rom;
    if (!readRom(romPath, rom)) {
        return 2;
    }
    cutenes::Machine machine;
    if (machine.loadCartridge(rom.data(), rom.size()).error
        != cutenes::LoadError::Ok) {
        return 2;
    }
    RecordingSink sink;
    sink.keepLast = true;
    machine.setVideoSink(&sink);
    machine.setAudioSink(&sink);
    machine.powerOn();
    machine.runFrames(frames);
    return writePpm(outPath, sink.last) ? 0 : 2;
}

// Debugging aid: report where the CPU is after N frames, sampling PC over the
// next few frames (a ROM stuck in a crash handler shows a tight loop).
// Environment variables select other views:
//   WHERE_TAIL=1     the 40 instructions executed before first entering
//                    $FF80-$FFE9 (Holy Mapperel's crash handler)
//   WHERE_WRITES=1   stores to mapper-register space ($8000+) as they run
//   WHERE_DIS=addr   40 instructions disassembled forward from a hex address
//   WHERE_TAGS=1     byte at +$FF8 of each 4K PRG window (bank tags)
//   WHERE_CHRTAGS=1  byte at +$1FC of each 1K CHR window (bank tags)
int cmdWhere(const char* romPath, std::uint32_t frames)
{
    std::vector<std::uint8_t> rom;
    if (!readRom(romPath, rom)) {
        return 2;
    }
    cutenes::Machine machine;
    if (machine.loadCartridge(rom.data(), rom.size()).error
        != cutenes::LoadError::Ok) {
        return 2;
    }
    machine.powerOn();
    if (std::getenv("WHERE_TAIL") != nullptr) {
        // Print the instructions executed just before the CPU first enters
        // the $FF80-$FFFF window (a crash handler), from a ring buffer.
        std::vector<std::string> ring;
        bool entered = false;
        machine.setInstructionTrace([&](const cutenes::TracePoint& tp) {
            if (entered) {
                return;
            }
            if (tp.cpu.pc >= 0xFF80 && tp.cpu.pc < 0xFFEA) {
                entered = true;
                for (const std::string& l : ring) {
                    std::printf("%s\n", l.c_str());
                }
                return;
            }
            ring.push_back(machine.disassemble(tp.cpu.pc).substr(0, 34));
            if (ring.size() > 40) {
                ring.erase(ring.begin());
            }
        });
        machine.runFrames(frames);
        return 0;
    }
    if (std::getenv("WHERE_WRITES") != nullptr) {
        // Log stores to mapper-register space ($8000+) as they execute.
        int shown = 0;
        machine.setInstructionTrace([&](const cutenes::TracePoint& tp) {
            const std::string line = machine.disassemble(tp.cpu.pc);
            const std::size_t st = line.find("ST");
            if (st == std::string::npos || shown >= 20000) {
                return;
            }
            const std::size_t dollar = line.find('$', st);
            if (dollar == std::string::npos || line.size() < dollar + 5) {
                return;
            }
            const unsigned long target =
                std::strtoul(line.substr(dollar + 1, 4).c_str(), nullptr, 16);
            if (target >= 0x8000 && line[st + 2] >= 'A' && line[st + 2] <= 'Y') {
                ++shown;
                std::printf("%s  A=%02x X=%02x Y=%02x\n",
                    line.substr(0, 28).c_str(), tp.cpu.a, tp.cpu.x, tp.cpu.y);
            }
        });
    }
    machine.runFrames(frames);
    if (std::getenv("WHERE_WRITES") != nullptr) {
        return 0;
    }
    if (std::getenv("WHERE_CHRTAGS") != nullptr) {
        // CHR bytes at $1FC of each 1K window, read through the PPU bus.
        for (std::uint16_t w = 0; w < 8; ++w) {
            std::uint8_t b = 0;
            machine.readVram(static_cast<std::uint16_t>(w * 0x400 + 0x1FC), 1, &b);
            std::printf("chr window %u (+1FC) = %02x\n", w, b);
        }
        return 0;
    }
    if (std::getenv("WHERE_TAGS") != nullptr) {
        // Byte at offset $FF8 of each 4K CPU window (Holy Mapperel bank tags).
        for (std::uint32_t a = 0x8000; a < 0x10000; a += 0x1000) {
            std::printf("window %04x tag(+FF8) = %02x\n", a,
                machine.peek(static_cast<std::uint16_t>(a + 0xFF8)));
        }
        return 0;
    }
    if (const char* dis = std::getenv("WHERE_DIS")) {
        // Walk instructions forward from a hex address (trace-format text).
        std::uint16_t addr = static_cast<std::uint16_t>(std::strtoul(dis, nullptr, 16));
        for (int i = 0; i < 40; ++i) {
            const std::string line = machine.disassemble(addr);
            std::printf("%s\n", line.substr(0, 40).c_str());
            // The trace line is "ADDR  B1 B2 B3  OP": count the hex bytes.
            std::size_t n = 0;
            for (std::size_t k = 6; k + 2 < line.size() && line[k] != ' ' ? true : (k + 1 < line.size() && line[k + 1] != ' '); k += 3) {
                ++n;
                if (n == 3) break;
            }
            addr = static_cast<std::uint16_t>(addr + (n ? n : 1));
        }
        return 0;
    }
    for (int i = 0; i < 6; ++i) {
        const cutenes::CpuState c = machine.cpu();
        std::printf("pc=%04x a=%02x x=%02x y=%02x s=%02x p=%02x  %s\n", c.pc,
            c.a, c.x, c.y, c.s, c.p, machine.disassemble(c.pc).c_str());
        machine.runFrames(1);
    }
    return 0;
}

// Print all four nametables as text (printable ASCII tile codes shown as
// characters, anything else as '.'), plus the raw tile codes of the first
// non-blank row, to learn how a ROM encodes its on-screen text.
int cmdText(const char* romPath, std::uint32_t frames)
{
    std::vector<std::uint8_t> rom;
    if (!readRom(romPath, rom)) {
        return 2;
    }
    cutenes::Machine machine;
    if (machine.loadCartridge(rom.data(), rom.size()).error
        != cutenes::LoadError::Ok) {
        return 2;
    }
    machine.powerOn();
    machine.runFrames(frames);
    for (int table = 0; table < 4; ++table) {
        std::vector<std::uint8_t> nt(0x3C0);
        machine.readVram(static_cast<std::uint16_t>(0x2000 + table * 0x400),
            nt.size(), nt.data());
        std::printf("--- nametable %d ---\n", table);
        for (std::size_t row = 0; row < 30; ++row) {
            std::string line;
            bool blank = true;
            for (std::size_t col = 0; col < 32; ++col) {
                const std::uint8_t c = nt[row * 32 + col];
                blank = blank && (c == 0 || c == 0x20);
                line += (c >= 0x20 && c <= 0x7E) ? static_cast<char>(c) : '.';
            }
            if (!blank) {
                std::printf("%2zu |%s|", row, line.c_str());
                if (std::getenv("TEXT_HEX") != nullptr) {
                    for (std::size_t col = 0; col < 32; ++col) {
                        std::printf(" %02x", nt[row * 32 + col]);
                    }
                }
                std::printf("\n");
            }
        }
    }
    return 0;
}

// T058 tier-2: run until the frame hash settles (a static result
// screen), reporting the settled hash — the baseline for golden capture.
// `expectHash` (when non-null) pins the settled frame hash: a ROM that
// settles on a different screen, such as a FAIL report, is a failure.
// `outPath` writes the settled screen as a PPM for manual inspection.
int cmdSettle(const char* romPath, std::uint32_t maxFrames,
    std::uint32_t minFrames, const char* expectHash, const char* outPath)
{
    std::vector<std::uint8_t> rom;
    if (!readRom(romPath, rom)) {
        return 2;
    }
    cutenes::Machine machine;
    if (machine.loadCartridge(rom.data(), rom.size()).error
        != cutenes::LoadError::Ok) {
        return 2;
    }
    RecordingSink sink;
    sink.keepLast = outPath != nullptr;
    machine.setVideoSink(&sink);
    machine.setAudioSink(&sink);
    machine.powerOn();

    constexpr std::uint32_t kStableRun = 30; // ~half a second of stillness
    std::uint32_t stable = 0;
    std::uint32_t frame = 0;
    for (; frame < maxFrames; ++frame) {
        machine.runFrames(1);
        if (sink.hashes.size() >= 2
            && sink.hashes.back() == sink.hashes[sink.hashes.size() - 2]) {
            // A long test can sit on a blank screen with rendering off;
            // don't settle before `minFrames` has elapsed.
            if (++stable >= kStableRun && frame >= minFrames) {
                break;
            }
        } else {
            stable = 0;
        }
    }
    const bool settled = stable >= kStableRun && frame >= minFrames;
    const std::uint32_t hash = sink.hashes.empty() ? 0 : sink.hashes.back();
    char hashText[16];
    std::snprintf(hashText, sizeof hashText, "%08x", hash);
    const bool matches = expectHash == nullptr
        || std::strcmp(expectHash, hashText) == 0;
    std::printf("settle: %s at frame %u hash %s%s\n",
        settled ? "SETTLED" : "TIMEOUT", frame, hashText,
        matches ? "" : " (MISMATCH: expected pinned hash)");
    if (outPath != nullptr && !writePpm(outPath, sink.last)) {
        std::fprintf(stderr, "settle: cannot write %s\n", outPath);
        return 2;
    }
    if (expectHash != nullptr && !matches) {
        std::printf("settle: expected %s\n", expectHash);
    }
    return settled && matches ? 0 : 1;
}

// T059/T046: scripted game checks. The list format is documented in
// tests/playtest/playtest.txt. Entries run in order; each records PASS/FAIL.
struct PlaytestStep {
    enum class Kind { Run, Reset, ExpectMem, ExpectChanges, ExpectText, ExpectHash };
    Kind kind = Kind::Run;
    std::string text;
    std::uint32_t frames = 0;
    cutenes::ButtonSet buttons;
    std::uint16_t addr = 0;
    std::uint32_t value = 0;
};

struct PlaytestEntry {
    std::string name;
    std::string rom;
    std::string expectError;
    int forceSubmapper = -1; // test-only: patch header to NES 2.0 + submapper
    int forceRegion = -1;    // test-only: patch header to NES 2.0 TV system (0 NTSC, 1 PAL)
    bool fontA1 = false; // tile codes 1..26 are letters A..Z (else ASCII)
    std::vector<PlaytestStep> steps;
};

bool parseButtons(const std::string& spec, cutenes::ButtonSet& out)
{
    static const char* const kNames[] = {
        "Right", "Left", "Down", "Up", "Start", "Select", "B", "A"};
    out.reset();
    if (spec == "none") {
        return true;
    }
    std::size_t start = 0;
    while (start <= spec.size()) {
        const std::size_t plus = spec.find('+', start);
        const std::string name = spec.substr(
            start, plus == std::string::npos ? std::string::npos : plus - start);
        bool found = false;
        for (std::size_t i = 0; i < 8; ++i) {
            if (name == kNames[i]) {
                out.set(i);
                found = true;
            }
        }
        if (!found) {
            return false;
        }
        if (plus == std::string::npos) {
            break;
        }
        start = plus + 1;
    }
    return true;
}

const char* loadErrorName(cutenes::LoadError e)
{
    switch (e) {
    case cutenes::LoadError::Ok: return "Ok";
    case cutenes::LoadError::NotAnInesFile: return "NotAnInesFile";
    case cutenes::LoadError::TruncatedFile: return "TruncatedFile";
    case cutenes::LoadError::InvalidHeader: return "InvalidHeader";
    case cutenes::LoadError::InvalidBankCounts: return "InvalidBankCounts";
    case cutenes::LoadError::UnsupportedMapper: return "UnsupportedMapper";
    case cutenes::LoadError::Nes2FieldInvalid: return "Nes2FieldInvalid";
    }
    return "Unknown";
}

bool parsePlaytest(const std::string& text, std::vector<PlaytestEntry>& out,
    std::string& error)
{
    std::istringstream lines(text);
    std::string line;
    int lineNo = 0;
    PlaytestEntry* current = nullptr;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') { // CRLF checkouts
            line.pop_back();
        }
        ++lineNo;
        const std::size_t hash = line.find('#');
        if (hash != std::string::npos) {
            line.erase(hash);
        }
        std::istringstream tokens(line);
        std::string cmd;
        if (!(tokens >> cmd)) {
            continue;
        }
        const auto fail = [&](const char* what) {
            error = "line " + std::to_string(lineNo) + ": " + what;
            return false;
        };
        if (cmd == "entry") {
            PlaytestEntry entry;
            if (!(tokens >> entry.name)) {
                return fail("entry needs a name");
            }
            out.push_back(entry);
            current = &out.back();
            continue;
        }
        if (current == nullptr) {
            return fail("command outside an entry");
        }
        if (cmd == "end") {
            current = nullptr;
        } else if (cmd == "rom") {
            if (!(tokens >> current->rom)) {
                return fail("rom needs a path");
            }
        } else if (cmd == "expect-error") {
            if (!(tokens >> current->expectError)) {
                return fail("expect-error needs a LoadError name");
            }
        } else if (cmd == "run") {
            PlaytestStep step;
            std::string buttons = "none";
            if (!(tokens >> step.frames)) {
                return fail("run needs a frame count");
            }
            tokens >> buttons;
            if (!parseButtons(buttons, step.buttons)) {
                return fail("unknown button name");
            }
            step.kind = PlaytestStep::Kind::Run;
            current->steps.push_back(step);
        } else if (cmd == "reset") {
            PlaytestStep step;
            step.kind = PlaytestStep::Kind::Reset;
            current->steps.push_back(step);
        } else if (cmd == "expect-mem") {
            PlaytestStep step;
            step.kind = PlaytestStep::Kind::ExpectMem;
            if (!(tokens >> std::hex >> step.addr >> step.value >> std::dec)) {
                return fail("expect-mem needs hex <addr> <value>");
            }
            current->steps.push_back(step);
        } else if (cmd == "force-submapper") {
            if (current == nullptr) {
                return fail("force-submapper outside an entry");
            }
            if (!(tokens >> current->forceSubmapper) || current->forceSubmapper < 0
                || current->forceSubmapper > 15) {
                return fail("force-submapper needs 0..15");
            }
        } else if (cmd == "region") {
            std::string name;
            if (current == nullptr || !(tokens >> name)
                || (name != "ntsc" && name != "pal")) {
                return fail("region needs 'ntsc' or 'pal' inside an entry");
            }
            current->forceRegion = name == "pal" ? 1 : 0;
        } else if (cmd == "font") {
            std::string name;
            if (current == nullptr || !(tokens >> name)
                || (name != "ascii" && name != "a1")) {
                return fail("font needs 'ascii' or 'a1' inside an entry");
            }
            current->fontA1 = name == "a1";
        } else if (cmd == "expect-text") {
            PlaytestStep step;
            step.kind = PlaytestStep::Kind::ExpectText;
            std::getline(tokens, step.text);
            const std::size_t first = step.text.find_first_not_of(' ');
            step.text = first == std::string::npos ? "" : step.text.substr(first);
            if (step.text.empty()) {
                return fail("expect-text needs text");
            }
            current->steps.push_back(step);
        } else if (cmd == "expect-hash") {
            PlaytestStep step;
            step.kind = PlaytestStep::Kind::ExpectHash;
            std::string hex;
            if (current == nullptr || !(tokens >> hex) || hex.size() != 8
                || hex.find_first_not_of("0123456789abcdefABCDEF")
                    != std::string::npos) {
                return fail("expect-hash needs 8 hex digits inside an entry");
            }
            step.value = static_cast<std::uint32_t>(std::strtoul(hex.c_str(), nullptr, 16));
            current->steps.push_back(step);
        } else if (cmd == "expect-changes") {
            PlaytestStep step;
            step.kind = PlaytestStep::Kind::ExpectChanges;
            if (!(tokens >> step.value)) {
                return fail("expect-changes needs a count");
            }
            current->steps.push_back(step);
        } else {
            return fail("unknown command");
        }
    }
    if (current != nullptr) {
        error = "entry '" + current->name + "' is missing 'end'";
        return false;
    }
    return true;
}

bool runPlaytestEntry(const std::string& dir, const PlaytestEntry& entry, const char* shotsDir,
    std::string& why)
{
    std::vector<std::uint8_t> rom;
    const std::string romPath = dir.empty() ? entry.rom : dir + "/" + entry.rom;
    if (!readRom(romPath.c_str(), rom)) {
        why = "cannot read " + romPath;
        return false;
    }
    if (entry.forceSubmapper >= 0 && rom.size() >= 16) {
        // Test-only: promote the header to NES 2.0 and select the submapper
        // (byte 7 bit 3; byte 13 low nibble). Keeps PRG/CHR sizes; RAM
        // bytes set to a safe 8K shift.
        rom[7] = static_cast<std::uint8_t>((rom[7] & 0xF0) | 0x08);
        rom[8] = 0;
        rom[9] = 0;
        rom[10] = 7; // 8K PRG-RAM shift
        rom[11] = 0;
        rom[12] = 0;
        rom[13] = static_cast<std::uint8_t>(
            (rom[13] & 0xF0)
            | ((entry.forceSubmapper >= 0 ? entry.forceSubmapper : 0) & 0x0F));
        rom[14] = 0;
        rom[15] = 0;
    }
    cutenes::Machine machine;
    if (entry.forceRegion >= 0) {
        // The core's region override (core-additions.md), as Emulation >
        // Region uses it; the header stays as the ROM has it.
        machine.setRegionOverride(entry.forceRegion == 1 ? cutenes::Region::Pal : cutenes::Region::Ntsc);
    }
    const auto result = machine.loadCartridge(rom.data(), rom.size());
    if (!entry.expectError.empty()) {
        if (std::string(loadErrorName(result.error)) == entry.expectError) {
            return true;
        }
        why = std::string("expected load error ") + entry.expectError
            + ", got " + loadErrorName(result.error);
        return false;
    }
    if (result.error != cutenes::LoadError::Ok) {
        why = std::string("load error ") + loadErrorName(result.error);
        return false;
    }
    RecordingSink sink;
    sink.keepLast = shotsDir != nullptr;
    machine.setVideoSink(&sink);
    machine.setAudioSink(&sink);
    machine.powerOn();

    std::uint32_t lastChanges = 0;
    int stepNumber = 0;
    for (const PlaytestStep& step : entry.steps) {
        ++stepNumber;
        switch (step.kind) {
        case PlaytestStep::Kind::Run: {
            const std::size_t before = sink.hashes.size();
            lastChanges = 0;
            for (std::uint32_t f = 0; f < step.frames; ++f) {
                machine.setController(cutenes::Port::One, step.buttons);
                machine.runFrames(1);
            }
            for (std::size_t i = std::max<std::size_t>(before, 1);
                 i < sink.hashes.size(); ++i) {
                if (sink.hashes[i] != sink.hashes[i - 1]) {
                    ++lastChanges;
                }
            }
            if (shotsDir != nullptr && !sink.last.empty()) {
                // Inspection aid: the frame at the end of this run step,
                // named <entry>-<step>-<hash>.ppm.
                char name[512];
                std::snprintf(name, sizeof name, "%s/%s-%02d-%08x.ppm", shotsDir,
                    entry.name.c_str(), stepNumber, sink.hashes.back());
                writePpm(name, sink.last);
            }
            break;
        }
        case PlaytestStep::Kind::Reset:
            machine.reset(); // the console's Reset button
            break;
        case PlaytestStep::Kind::ExpectHash: {
            const std::uint32_t actual =
                sink.hashes.empty() ? 0 : sink.hashes.back();
            if (actual != step.value) {
                char buf[96];
                std::snprintf(buf, sizeof buf,
                    "screen hash %08x, pinned %08x", actual, step.value);
                why = buf;
                return false;
            }
            break;
        }
        case PlaytestStep::Kind::ExpectMem: {
            const std::uint8_t actual = machine.peek(step.addr);
            if (actual != step.value) {
                char buf[96];
                std::snprintf(buf, sizeof buf,
                    "mem[%04X] = %02X, expected %02X", step.addr, actual,
                    static_cast<unsigned>(step.value));
                why = buf;
                return false;
            }
            break;
        }
        case PlaytestStep::Kind::ExpectText: {
            // Nametable 0 tiles that are printable ASCII read as their
            // character (the ROM text console uses ASCII tile codes). With
            // `font a1`, codes 1..26 read as the letters A..Z (Holy Mapperel).
            std::vector<std::uint8_t> nt(0x3C0);
            machine.readVram(0x2000, 0x3C0, nt.data());
            std::string screen;
            for (std::size_t i = 0; i < nt.size(); ++i) {
                const std::uint8_t c = nt[i];
                if (entry.fontA1 && c >= 1 && c <= 26) {
                    screen += static_cast<char>('A' + c - 1);
                } else {
                    screen += (c >= 0x20 && c <= 0x7E) ? static_cast<char>(c) : ' ';
                }
                if (i % 32 == 31) {
                    screen += '\n';
                }
            }
            if (screen.find(step.text) == std::string::npos) {
                why = "screen does not show \"" + step.text + "\"";
                return false;
            }
            break;
        }
        case PlaytestStep::Kind::ExpectChanges:
            if (lastChanges < step.value) {
                why = "screen changed " + std::to_string(lastChanges)
                    + " times, expected at least " + std::to_string(step.value);
                return false;
            }
            break;
        }
    }
    return true;
}

// `only` selects a single entry by name (nullptr runs all). Exit codes: 0 all
// ran and passed, 1 a failure, 2 usage/parse error, 77 nothing ran because
// every selected entry needs a ROM absent from roms-local/ (ctest SKIP).
int cmdPlaytest(const char* listPath, const char* only, const char* shotsDir)
{
    std::vector<std::uint8_t> bytes;
    if (!readRom(listPath, bytes)) {
        return 2;
    }
    const std::string text(bytes.begin(), bytes.end());
    std::vector<PlaytestEntry> entries;
    std::string error;
    if (!parsePlaytest(text, entries, error)) {
        std::printf("playtest: %s: %s\n", listPath, error.c_str());
        return 2;
    }
    std::string dir;
    const std::string listStr = listPath;
    const std::size_t slash = listStr.find_last_of('/');
    if (slash != std::string::npos) {
        dir = listStr.substr(0, slash);
    }
    int failures = 0;
    int skipped = 0;
    int ran = 0;
    int selected = 0;
    for (const PlaytestEntry& entry : entries) {
        if (only != nullptr && entry.name != only) {
            continue;
        }
        ++selected;
        // Commercial titles live in the gitignored roms-local/ folder; a
        // checkout without them skips those entries instead of failing.
        const std::string romPath = dir.empty() ? entry.rom : dir + "/" + entry.rom;
        const bool localOnly = entry.rom.find("roms-local/") != std::string::npos;
        std::FILE* probe = std::fopen(romPath.c_str(), "rb");
        if (probe != nullptr) {
            std::fclose(probe);
        } else if (localOnly) {
            ++skipped;
            std::printf("playtest: %s: SKIP (ROM not present: %s)\n",
                entry.name.c_str(), romPath.c_str());
            continue;
        }
        std::string why;
        const bool ok = runPlaytestEntry(dir, entry, shotsDir, why);
        ++ran;
        if (ok) {
            std::printf("playtest: %s: PASS\n", entry.name.c_str());
        } else {
            ++failures;
            std::printf("playtest: %s: FAIL (%s)\n", entry.name.c_str(), why.c_str());
        }
    }
    if (only != nullptr && selected == 0) {
        std::printf("playtest: no entry named '%s'\n", only);
        return 2;
    }
    std::printf("playtest: %d entries, %d skipped, %d failed\n",
        selected, skipped, failures);
    if (failures != 0) {
        return 1;
    }
    return ran == 0 && skipped > 0 ? 77 : 0;
}

// T065 cross-platform golden hashes: run vendored ROMs for fixed frame
// counts and print one stable line each. `--check FILE` compares against a
// checked-in baseline generated on the reference platform.
int cmdGolden(const char* listPath, const char* checkPath)
{
    std::vector<std::uint8_t> bytes;
    if (!readRom(listPath, bytes)) {
        return 2;
    }
    const std::string text(bytes.begin(), bytes.end());
    std::string dir;
    const std::string listStr = listPath;
    const std::size_t slash = listStr.find_last_of('/');
    if (slash != std::string::npos) {
        dir = listStr.substr(0, slash);
    }
    std::istringstream lines(text);
    std::string line;
    std::vector<std::string> out;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') { // CRLF checkouts
            line.pop_back();
        }
        const std::size_t hash = line.find_first_not_of(" \t");
        if (hash == std::string::npos || line[hash] == '#') {
            continue;
        }
        std::istringstream fields(line);
        std::string name, rom;
        std::uint32_t frames = 0;
        if (!(fields >> name >> rom >> frames)) {
            std::printf("golden: bad line: %s\n", line.c_str());
            return 2;
        }
        const std::string romPath =
            dir.empty() ? rom : dir + "/" + rom;
        std::vector<std::uint8_t> romBytes;
        if (!readRom(romPath.c_str(), romBytes)) {
            std::printf("golden: %s: cannot read %s\n", name.c_str(),
                romPath.c_str());
            return 2;
        }
        RecordingSink sink;
        cutenes::Machine machine;
        const auto result =
            machine.loadCartridge(romBytes.data(), romBytes.size());
        if (result.error != cutenes::LoadError::Ok) {
            std::printf("golden: %s: load error %d\n", name.c_str(),
                static_cast<int>(result.error));
            return 2;
        }
        machine.setVideoSink(&sink);
        machine.setAudioSink(&sink);
        machine.powerOn();
        machine.runFrames(frames);
        char buf[160];
        std::snprintf(buf, sizeof buf, "golden: %-18s last=%08x audio=%llu",
            name.c_str(),
            sink.hashes.empty() ? 0 : sink.hashes.back(),
            static_cast<unsigned long long>(sink.audio.size()));
        out.push_back(buf);
    }
    for (const std::string& o : out) {
        std::printf("%s\n", o.c_str());
    }
    if (checkPath == nullptr) {
        return 0;
    }
    std::vector<std::uint8_t> base;
    if (!readRom(checkPath, base)) {
        std::printf("golden: cannot read baseline %s\n", checkPath);
        return 2;
    }
    std::string all(reinterpret_cast<const char*>(base.data()), base.size());
    std::istringstream wantLines(all);
    std::vector<std::string> expected;
    std::string l;
    while (std::getline(wantLines, l)) {
        if (!l.empty() && l.back() == '\r') {
            l.pop_back();
        }
        if (l.find("golden:") == 0) {
            expected.push_back(l);
        }
    }
    if (expected != out) {
        std::printf("golden: MISMATCH against %s\n", checkPath);
        return 1;
    }
    std::printf("golden: baseline match (%zu entries)\n", out.size());
    return 0;
}

int notImplemented(const char* command, const char* task)
{
    std::fprintf(stderr,
        "rom_harness: '%s' is not implemented yet (lands with task %s)\n",
        command, task);
    return 2;
}

void usage()
{
    std::fprintf(stderr,
        "usage: rom_harness <command> [args]\n"
        "  run <rom> <frames>              run N frames, report frame hashes\n"
        "  determinism --rom R --frames N  run-twice byte comparison (T027)\n"
        "  suite [--tier 1|2]              tiered test-ROM acceptance (T031)\n"
        "  nestest [--golden-log]          CPU golden-log verification (T029)\n"
        "  savestate --rom R --at F --rerun-to F   restore round trip (T051)\n"
        "  settle ROM [--min-frames N] [--expect HASH] [--out F.ppm]\n"
        "  shot ROM FRAMES OUT.ppm              frame after N frames as PPM\n"
        "  text ROM FRAMES / where ROM FRAMES   debugging views (see source)\n"
        "  playtest --list FILE [--entry N] [--shots DIR]  scripted checks (T059)\n"
        "  bench --rom R [--seconds N] [--min-x X]  sustained speed (T061); exit 1 below X x real-time\n"
        "  golden --list F [--check B]     cross-platform golden hashes (T065)\n");
}

std::uint32_t parseUint(const char* s)
{
    return static_cast<std::uint32_t>(std::strtoul(s, nullptr, 10));
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        usage();
        return 2;
    }
    const std::string command = argv[1];

    if (command == "run" && argc == 4) {
        return cmdRun(argv[2], parseUint(argv[3]));
    }
    if (command == "savestate" && argc == 8
        && std::strcmp(argv[2], "--rom") == 0
        && std::strcmp(argv[4], "--at") == 0
        && std::strcmp(argv[6], "--rerun-to") == 0) {
        return cmdSavestate(argv[3], parseUint(argv[5]), parseUint(argv[7]));
    }
    if (command == "golden" && argc >= 4 && std::strcmp(argv[2], "--list") == 0) {
        const char* check = nullptr;
        if (argc == 6 && std::strcmp(argv[4], "--check") == 0) {
            check = argv[5];
        } else if (argc != 4) {
            usage();
            return 2;
        }
        return cmdGolden(argv[3], check);
    }
    if (command == "shot" && argc == 5) {
        return cmdShot(argv[2], parseUint(argv[3]), argv[4]);
    }
    if (command == "where" && argc == 4) {
        return cmdWhere(argv[2], parseUint(argv[3]));
    }
    if (command == "text" && argc == 4) {
        return cmdText(argv[2], parseUint(argv[3]));
    }
    if (command == "settle" && argc >= 3) {
        const char* expect = nullptr;
        const char* out = nullptr;
        std::uint32_t minFrames = 0;
        for (int i = 3; i + 1 < argc; i += 2) {
            if (std::strcmp(argv[i], "--min-frames") == 0) {
                minFrames = parseUint(argv[i + 1]);
            }
            if (std::strcmp(argv[i], "--expect") == 0) {
                expect = argv[i + 1];
            } else if (std::strcmp(argv[i], "--out") == 0) {
                out = argv[i + 1];
            }
        }
        return cmdSettle(argv[2], 1800, minFrames, expect, out); // ~30s cap
    }
    if (command == "bench" && argc >= 4
        && std::strcmp(argv[2], "--rom") == 0) {
        // Optional: --seconds N (default 5), --min-x X (default 2.0 = the
        // spec's 2x real-time headroom; CI passes a looser floor).
        std::uint32_t seconds = 5;
        double minX = 2.0;
        for (int i = 4; i + 1 < argc; i += 2) {
            if (std::strcmp(argv[i], "--seconds") == 0) {
                seconds = parseUint(argv[i + 1]);
            } else if (std::strcmp(argv[i], "--min-x") == 0) {
                minX = std::strtod(argv[i + 1], nullptr);
            }
        }
        return cmdBench(argv[3], seconds, minX);
    }
    if (command == "step-equiv" && argc == 4) {
        return cmdStepEquiv(argv[2], parseUint(argv[3]));
    }
    if (command == "determinism" && argc == 6
        && std::strcmp(argv[2], "--rom") == 0
        && std::strcmp(argv[4], "--frames") == 0) {
        return cmdDeterminism(argv[3], parseUint(argv[5]));
    }
    if (command == "suite" && argc == 4
        && std::strcmp(argv[2], "--rom") == 0) {
        return cmdSuite(argv[3]);
    }
    if (command == "suite") {
        std::fprintf(stderr, "usage: rom_harness suite --rom <path>\n");
        return 2;
    }
    if (command == "nestest") return cmdNestest(argc, argv);
    if (command == "playtest" && argc >= 4 && std::strcmp(argv[2], "--list") == 0) {
        const char* entry = nullptr;
        const char* shots = nullptr;
        for (int i = 4; i + 1 < argc; i += 2) {
            if (std::strcmp(argv[i], "--entry") == 0) {
                entry = argv[i + 1];
            } else if (std::strcmp(argv[i], "--shots") == 0) {
                shots = argv[i + 1];
            }
        }
        return cmdPlaytest(argv[3], entry, shots);
    }
    if (command == "playtest") {
        std::fprintf(stderr,
            "usage: rom_harness playtest --list FILE [--entry NAME] [--shots DIR]\n");
        return 2;
    }

    usage();
    return 2;
}
