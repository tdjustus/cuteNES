// rom_harness — headless verification driver for the cutenes-core
// (specs/001-emulation-core/quickstart.md scenarios).
//
// Phase-2 state: the `run` scenario exercises the machine skeleton (black
// frames, silent audio, deterministic frame hashing). ROM-dependent
// scenarios activate as their phases land:
//   nestest      task T029    determinism  task T027
//   suite        task T031    savestate    task T051
//   playtest     task T059    bench        task T061

#include <cutenes/cutenes.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

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

class HashingVideoSink final : public cutenes::IVideoSink {
public:
    void onFrame(const cutenes::VideoFrame& frame) override
    {
        const auto* bytes =
            reinterpret_cast<const std::uint8_t*>(frame.pixels);
        const std::uint32_t hash =
            fnv1a32(bytes, 256 * 240 * 4); // XRGB8888, 61440 bytes
        std::printf("frame %llu hash %08x\n",
            static_cast<unsigned long long>(frame.frameIndex), hash);
        ++m_frames;
        m_lastHash = hash;
    }

    std::uint64_t frames() const { return m_frames; }
    std::uint32_t lastHash() const { return m_lastHash; }

private:
    std::uint64_t m_frames = 0;
    std::uint32_t m_lastHash = 0;
};

class CountingAudioSink final : public cutenes::IAudioSink {
public:
    void onSamples(const std::int16_t* /*pcm*/, std::size_t count,
        std::uint64_t /*startMasterCycle*/) override
    {
        m_samples += count;
    }

    std::uint64_t samples() const { return m_samples; }

private:
    std::uint64_t m_samples = 0;
};

int cmdRun(std::uint32_t frames)
{
    cutenes::Machine machine;
    HashingVideoSink video;
    CountingAudioSink audio;
    machine.setVideoSink(&video);
    machine.setAudioSink(&audio);

    machine.powerOn();
    machine.runFrames(frames);

    std::printf("frames=%llu audio_samples=%llu last_hash=%08x\n",
        static_cast<unsigned long long>(video.frames()),
        static_cast<unsigned long long>(audio.samples()),
        video.lastHash());
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
        "  run <frames>                 run the machine N frames and report hashes\n"
        "  suite [--tier 1|2]           tiered test-ROM acceptance (T031)\n"
        "  nestest [--golden-log]       CPU golden-log verification (T029)\n"
        "  determinism --rom R --frames N  replay determinism (T027)\n"
        "  savestate --rom R --at F --rerun-to F  restore round trip (T051)\n"
        "  playtest --list FILE         scripted commercial-game checks (T059)\n"
        "  bench --rom R --seconds N    sustained-speed benchmark (T061)\n");
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        usage();
        return 2;
    }
    const std::string command = argv[1];

    if (command == "run" && argc == 3) {
        return cmdRun(static_cast<std::uint32_t>(std::strtoul(argv[2],
            nullptr, 10)));
    }
    if (command == "suite") return notImplemented("suite", "T031");
    if (command == "nestest") return notImplemented("nestest", "T029");
    if (command == "determinism") return notImplemented("determinism", "T027");
    if (command == "savestate") return notImplemented("savestate", "T051");
    if (command == "playtest") return notImplemented("playtest", "T059");
    if (command == "bench") return notImplemented("bench", "T061");

    usage();
    return 2;
}
