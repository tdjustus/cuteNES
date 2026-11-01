// Q2 in-process (tasks A018, SC-004/SC-008): every function in the core
// contract's capability table has its command. The table is parsed from
// the markdown, so a new core row without a command fails here.

#include "support.hpp"

#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>

using namespace cutenes::api;
using namespace cutenes::api::test;

namespace {

// Core function -> the command that exposes it (command-catalog.md).
const std::map<std::string, std::string>& commandFor()
{
    static const std::map<std::string, std::string> m = {
        {"loadCartridge", "core.load_rom"}, {"ejectCartridge", "core.eject"},
        {"cartridgeInfo", "core.cartridge_info"}, {"setMmc3Revision", "core.set_mmc3_revision"},
        {"mmc3Revision", "core.get_mmc3_revision"}, {"runFrames", "core.run_frames"},
        {"frameAdvance", "core.frame_advance"}, {"pause", "core.pause"},
        {"resume", "core.resume"}, {"stepInstruction", "core.step_instruction"},
        {"powerOn", "core.power_on"}, {"reset", "core.reset"},
        {"setController", "core.set_controller"}, {"controller", "core.get_controller"},
        {"captureState", "core.capture_state"}, {"loadState", "core.load_state"},
        {"readMemory", "core.read_memory"}, {"writeMemory", "core.write_memory"},
        {"writeCpuRegister", "core.write_cpu_register"}, {"cpu", "core.get_cpu"},
        {"IVideoSink::onFrame", "core.screen"}, {"IAudioSink::onSamples", "core.audio"},
        {"region", "core.region"}, {"setRegionOverride", "core.set_region_override"},
        {"regionOverride", "core.region"}, {"setPalette", "core.set_palette"},
        {"setGameGenieCodes", "core.set_game_genie"}, {"decodeGameGenie", "core.decode_game_genie"},
        {"frameCounter", "core.counters"},
        {"masterCycle", "core.counters"}, {"runState", "core.run_state"},
        {"sram", "core.read_sram"}, {"loadSram", "core.load_sram"}, {"writeCpu", "core.set_cpu"},
        {"readPpuRegisters", "core.read_ppu_registers"}, {"readVram", "core.read_vram"},
        {"writeVram", "core.write_vram"}, {"readOam", "core.read_oam"},
        {"writeOam", "core.write_oam"}, {"readPalette", "core.read_palette"},
        {"writePalette", "core.write_palette"}, {"peek", "core.peek"},
        {"disassemble", "core.disassemble"}, {"memoryDump", "core.dump_memory"},
        {"dumpMemory", "session.write_dump"}, {"setInstructionTrace", "core.trace"},
        {"debugSetProgramCounter", "core.set_cpu"}, {"coreVersionString", "api.version"},
    };
    return m;
}

std::vector<std::string> capabilityFunctions()
{
    std::ifstream in(sourceDir() + "/specs/001-emulation-core/contracts/core-api.md");
    std::string line;
    bool inTable = false;
    std::vector<std::string> out;
    const std::regex code("`([^`]+)`");
    while (std::getline(in, line)) {
        if (line.rfind("| Capability", 0) == 0) {
            inTable = true;
            continue;
        }
        if (!inTable) continue;
        if (line.empty() || line[0] != '|') break;
        if (line.rfind("|---", 0) == 0) continue;
        const auto apiColumn = line.substr(line.find('|', 1));
        // "(+ slot mgmt is consumer-side naming)" style notes hold no code.
        const auto noteStart = apiColumn.find("(+");
        const auto api = apiColumn.substr(0, noteStart);
        for (std::sregex_iterator it(api.begin(), api.end(), code), end; it != end; ++it) {
            const std::string calls = std::regex_replace((*it)[1].str(), std::regex("\\([^)]*\\)"), "");
            std::istringstream words(calls);
            std::string word;
            while (words >> word) {
                word = word.substr(0, word.find('('));
                if (!word.empty()) out.push_back(word);
            }
        }
    }
    return out;
}

} // namespace

TEST(ApiParity, EveryCapabilityRowHasACommand)
{
    const auto functions = capabilityFunctions();
    ASSERT_GE(functions.size(), 40u) << "the capability table was not found or not parsed";
    Session s(testConfig());
    const auto names = s.registry().names();
    const std::set<std::string> registered(names.begin(), names.end());
    for (const auto& f : functions) {
        const auto it = commandFor().find(f);
        if (it == commandFor().end()) {
            ADD_FAILURE() << "core-api.md lists " << f << " but no command maps it";
            continue;
        }
        EXPECT_TRUE(registered.count(it->second)) << f << " -> " << it->second;
    }
}

TEST(ApiParity, EveryCatalogedCoreCommandIsRegistered)
{
    // command-catalog.md's core.* rows, as written in the contract.
    std::ifstream in(sourceDir() + "/specs/002-ai-mcp-server/contracts/command-catalog.md");
    std::stringstream text;
    text << in.rdbuf();
    const std::string doc = text.str();
    const std::regex row("`((core|session|api)\\.[a-z0-9_]+)`");
    Session s(testConfig());
    const auto names = s.registry().names();
    const std::set<std::string> registered(names.begin(), names.end());
    std::set<std::string> documented;
    for (std::sregex_iterator it(doc.begin(), doc.end(), row), end; it != end; ++it) {
        documented.insert((*it)[1].str());
    }
    for (const auto& name : documented) {
        EXPECT_TRUE(registered.count(name)) << name << " is in command-catalog.md but not registered";
    }
    for (const auto& name : registered) {
        EXPECT_TRUE(documented.count(name)) << name << " is registered but not in command-catalog.md";
    }
}
