// nestest golden-log verification (task T029).
//
// Automation mode: PC=$C000, results in $02 (official) / $03 (unofficial),
// per-instruction trace diffed against the vendored nestest.log. The first
// divergent line is reported with both versions.

#include "cutenes/cpu/trace.hpp"

#include <cutenes/cutenes.hpp>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace cutenes {
class NestestRunner {
public:
    static int run(const char* romPath, const char* logPath,
        bool printLines);
};
} // namespace cutenes

namespace {

bool readFile(const char* path, std::vector<std::uint8_t>& out)
{
    std::FILE* f = std::fopen(path, "rb");
    if (f == nullptr) {
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(static_cast<std::size_t>(size));
    const std::size_t got = std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    return got == out.size();
}

std::vector<std::string> readLog(const char* path)
{
    std::vector<std::uint8_t> data;
    std::vector<std::string> lines;
    if (!readFile(path, data)) {
        return lines;
    }
    std::string current;
    for (const std::uint8_t c : data) {
        if (c == '\n') {
            lines.push_back(current);
            current.clear();
        } else if (c != '\r') {
            current += static_cast<char>(c);
        }
    }
    if (!current.empty()) {
        lines.push_back(current);
    }
    return lines;
}

cutenes::Machine* g_machine = nullptr;

std::uint8_t busPeek(std::uint16_t addr)
{
    return g_machine != nullptr ? g_machine->debugPeek(addr) : 0;
}

} // namespace

int cutenes::NestestRunner::run(const char* romPath, const char* logPath,
    bool printLines)
{
    std::vector<std::uint8_t> rom;
    if (!readFile(romPath, rom)) {
        std::fprintf(stderr, "nestest: cannot open %s\n", romPath);
        return 2;
    }
    const auto golden = readLog(logPath);
    if (golden.empty()) {
        std::fprintf(stderr, "nestest: cannot read %s\n", logPath);
        return 2;
    }

    cutenes::Machine machine;
    const auto result = machine.loadCartridge(rom.data(), rom.size());
    if (result.error != cutenes::LoadError::Ok) {
        std::fprintf(stderr, "nestest: load error %d\n",
            static_cast<int>(result.error));
        return 2;
    }
    g_machine = &machine;
    machine.powerOn();
    machine.debugSetProgramCounter(0xC000); // automation entry

    std::size_t index = 0;
    std::string actual;
    machine.setInstructionTrace(
        [&](const cutenes::TracePoint& tp) {
            actual = cutenes::traceLine(tp, &busPeek);
        });

    while (index < golden.size()) {
        machine.stepInstruction();
        if (printLines) {
            std::printf("%s\n", actual.c_str());
        }
        if (actual != golden[index]) {
            std::printf(
                "nestest: DIVERGENCE at line %zu (instruction %zu)\n"
                "  golden: %s\n"
                "  actual: %s\n",
                index + 1, index, golden[index].c_str(), actual.c_str());
            return 1;
        }
        ++index;
    }

    std::uint8_t results[2] = {0, 0};
    machine.readMemory(0x02, 2, results);
    std::printf("nestest: %zu/%zu lines matched; $02=%02X $03=%02X\n",
        index, golden.size(), results[0], results[1]);
    return (results[0] == 0 && results[1] == 0) ? 0 : 1;
}
