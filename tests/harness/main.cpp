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

// T058 tier-2: run until the frame hash settles (a static result
// screen), reporting the settled hash — the baseline for golden capture.
int cmdSettle(const char* romPath, std::uint32_t maxFrames)
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

    constexpr std::uint32_t kStableRun = 30; // ~half a second of stillness
    std::uint32_t stable = 0;
    std::uint32_t frame = 0;
    for (; frame < maxFrames; ++frame) {
        machine.runFrames(1);
        if (sink.hashes.size() >= 2
            && sink.hashes.back() == sink.hashes[sink.hashes.size() - 2]) {
            if (++stable >= kStableRun) {
                break;
            }
        } else {
            stable = 0;
        }
    }
    const bool settled = stable >= kStableRun;
    std::printf("settle: %s at frame %u hash %08x\n",
        settled ? "SETTLED" : "TIMEOUT", frame,
        sink.hashes.empty() ? 0 : sink.hashes.back());
    return settled ? 0 : 1;
}

// T059/T046: scripted game checks. The list format is documented in
// tests/playtest/playtest.txt. Entries run in order; each records PASS/FAIL.
struct PlaytestStep {
    enum class Kind { Run, ExpectMem, ExpectChanges, ExpectText };
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

bool runPlaytestEntry(const std::string& dir, const PlaytestEntry& entry,
    std::string& why)
{
    std::vector<std::uint8_t> rom;
    const std::string romPath = dir.empty() ? entry.rom : dir + "/" + entry.rom;
    if (!readRom(romPath.c_str(), rom)) {
        why = "cannot read " + romPath;
        return false;
    }
    if (entry.forceSubmapper >= 0 && rom.size() >= 16) {
        // Test-only: promote the header to NES 2.0 and select the
        // submapper (byte 7 bit 3; byte 13 low nibble). Keeps PRG/CHR
        // sizes; RAM bytes set to a safe 8K shift.
        rom[7] = static_cast<std::uint8_t>((rom[7] & 0xF0) | 0x08);
        rom[8] = 0;
        rom[9] = 0;
        rom[10] = 7; // 8K PRG-RAM shift
        rom[11] = 0;
        rom[12] = 0; // NTSC
        rom[13] = static_cast<std::uint8_t>(
            (rom[13] & 0xF0) | (entry.forceSubmapper & 0x0F));
        rom[14] = 0;
        rom[15] = 0;
    }
    cutenes::Machine machine;
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
    machine.setVideoSink(&sink);
    machine.setAudioSink(&sink);
    machine.powerOn();

    std::uint32_t lastChanges = 0;
    for (const PlaytestStep& step : entry.steps) {
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
            // character (the ROM text console uses ASCII tile codes).
            std::vector<std::uint8_t> nt(0x3C0);
            machine.readVram(0x2000, 0x3C0, nt.data());
            std::string screen;
            for (std::size_t i = 0; i < nt.size(); ++i) {
                const std::uint8_t c = nt[i];
                screen += (c >= 0x20 && c <= 0x7E) ? static_cast<char>(c) : ' ';
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

int cmdPlaytest(const char* listPath)
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
    for (const PlaytestEntry& entry : entries) {
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
        const bool ok = runPlaytestEntry(dir, entry, why);
        if (ok) {
            std::printf("playtest: %s: PASS\n", entry.name.c_str());
        } else {
            ++failures;
            std::printf("playtest: %s: FAIL (%s)\n", entry.name.c_str(), why.c_str());
        }
    }
    std::printf("playtest: %zu entries, %d skipped, %d failed\n",
        entries.size(), skipped, failures);
    return failures == 0 ? 0 : 1;
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
        "  playtest --list FILE            scripted game checks (T059)\n"
        "  bench --rom R                   5s sustained-speed benchmark (T061)\n"
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
    if (command == "settle" && argc == 3) {
        return cmdSettle(argv[2], 1800); // ~30s emulated cap
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
    if (command == "playtest" && argc == 4
        && std::strcmp(argv[2], "--list") == 0) {
        return cmdPlaytest(argv[3]);
    }
    if (command == "playtest") {
        std::fprintf(stderr, "usage: rom_harness playtest --list FILE\n");
        return 2;
    }

    usage();
    return 2;
}
