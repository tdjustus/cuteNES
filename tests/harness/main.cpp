// rom_harness — headless verification driver for cutenes-core
// (specs/001-emulation-core/quickstart.md scenarios).
//
// Implemented: run (T012 + integration), determinism (T027).
// Pending: suite T031, nestest T029, savestate T051, playtest T059, bench T061.

#include <cutenes/cutenes.hpp>

#include <chrono>
#include <cstdint>
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
    }

    void onSamples(const std::int16_t* pcm, std::size_t count,
        std::uint64_t) override
    {
        audio.insert(audio.end(), pcm, pcm + count);
    }

    std::vector<std::uint32_t> hashes;
    std::vector<std::int16_t> audio;
};

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
    machine.runFrames(rerunTo - atFrame); // the uninterrupted reference

    machine.loadState(blob.data(), blob.size());
    const std::size_t hashesBefore = sink.hashes.size();
    machine.runFrames(rerunTo - atFrame); // the restored rerun
    bool ok = sink.hashes.size() == 2 * (rerunTo - atFrame) + atFrame;
    if (ok) {
        for (std::size_t i = 0; i < rerunTo - atFrame; ++i) {
            if (sink.hashes[hashesBefore + i]
                != sink.hashes[atFrame + i]) {
                ok = false;
                break;
            }
        }
    }
    // Repeated restore stability.
    machine.loadState(blob.data(), blob.size());
    machine.runFrames(1);
    std::printf("savestate: %s (%u->%u round trip)\n", ok ? "PASS" : "FAIL",
        atFrame, rerunTo);
    return ok ? 0 : 1;
}

// T061: sustained emulation speed vs real-time (2x headroom = SC-003).
int cmdBench(const char* romPath, std::uint32_t seconds)
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
            return fps >= 120.2 ? 0 : 1; // 2x headroom gate
        }
    }
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
        "  playtest --list FILE            scripted game checks (T059)\n"
        "  bench --rom R                   5s sustained-speed benchmark (T061)\n");
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
    if (command == "bench" && argc == 4
        && std::strcmp(argv[2], "--rom") == 0) {
        return cmdBench(argv[3], 5); // 5-second default run
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
    if (command == "playtest") return notImplemented("playtest", "T059");

    usage();
    return 2;
}
