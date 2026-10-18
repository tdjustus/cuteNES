// rom_harness — headless verification driver for cutenes-core
// (specs/001-emulation-core/quickstart.md scenarios).
//
// Implemented: run (T012 + integration), determinism (T027).
// Pending: suite T031, nestest T029, savestate T051, playtest T059, bench T061.

#include <cutenes/cutenes.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

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
        "  savestate --rom R --at F --rerun-to F  restore round trip (T051)\n"
        "  playtest --list FILE            scripted game checks (T059)\n"
        "  bench --rom R --seconds N       sustained-speed benchmark (T061)\n");
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
    if (command == "determinism" && argc == 6
        && std::strcmp(argv[2], "--rom") == 0
        && std::strcmp(argv[4], "--frames") == 0) {
        return cmdDeterminism(argv[3], parseUint(argv[5]));
    }
    if (command == "suite") return notImplemented("suite", "T031");
    if (command == "nestest") return notImplemented("nestest", "T029");
    if (command == "savestate") return notImplemented("savestate", "T051");
    if (command == "playtest") return notImplemented("playtest", "T059");
    if (command == "bench") return notImplemented("bench", "T061");

    usage();
    return 2;
}
