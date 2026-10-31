// core.*: one command per public Machine operation (command-catalog.md).

#include "session_impl.hpp"

#include <cutenes/api/schema.hpp>
#include <cutenes/version.hpp>

#include <filesystem>
#include <fstream>

namespace cutenes::api {

namespace s = schema;

namespace {

constexpr const char* kButtonNames[8] = {
    "Right", "Left", "Down", "Up", "Start", "Select", "B", "A"};

Json bytesResult(const std::vector<std::uint8_t>& v)
{
    return {{"bytes", media::base64Encode(v)}};
}

ButtonSet parseButtons(const Json& names)
{
    ButtonSet set;
    for (const auto& n : names) {
        for (std::size_t i = 0; i < 8; ++i) {
            if (n == kButtonNames[i]) {
                set.set(i);
            }
        }
    }
    return set;
}

Json buttonsJson(ButtonSet set)
{
    Json out = Json::array();
    for (std::size_t i = 0; i < 8; ++i) {
        if (set.test(i)) {
            out.push_back(kButtonNames[i]);
        }
    }
    return out;
}

Json cpuJson(const CpuState& c)
{
    return {{"a", c.a}, {"x", c.x}, {"y", c.y}, {"s", c.s}, {"p", c.p}, {"pc", c.pc}};
}

Json cartridgeSchema()
{
    return s::object({{"format", s::stringEnum({"iNES", "NES 2.0"})}, {"mapper", s::u16()},
                         {"submapper", s::u8()},
                         {"mirroring", s::stringEnum({"Horizontal", "Vertical", "FourScreen",
                                           "MapperControlled"})},
                         {"battery", s::boolean()}, {"prgSize", s::u64()}, {"chrSize", s::u64()},
                         {"prgRamSize", s::u64()},
                         {"region", s::stringEnum({"NTSC", "PAL", "Unspecified"})}},
        {"format", "mapper", "submapper", "mirroring", "battery", "prgSize", "chrSize",
            "prgRamSize", "region"});
}

Json cpuSchema()
{
    return s::object({{"a", s::u8()}, {"x", s::u8()}, {"y", s::u8()},
                         {"s", s::u8()}, {"p", s::u8()}, {"pc", s::u16()}},
        {"a", "x", "y", "s", "p", "pc"});
}

const char* regionName(Region r)
{
    switch (r) {
    case Region::Ntsc: return "NTSC";
    case Region::Pal: return "PAL";
    case Region::Unspecified: break;
    }
    return "Unspecified";
}

const char* loadErrorName(LoadError e)
{
    switch (e) {
    case LoadError::Ok: return "Ok";
    case LoadError::NotAnInesFile: return "NotAnInesFile";
    case LoadError::TruncatedFile: return "TruncatedFile";
    case LoadError::InvalidHeader: return "InvalidHeader";
    case LoadError::InvalidBankCounts: return "InvalidBankCounts";
    case LoadError::UnsupportedMapper: return "UnsupportedMapper";
    case LoadError::Nes2FieldInvalid: return "Nes2FieldInvalid";
    }
    return "Unknown";
}

const char* loadStateErrorName(LoadStateError e)
{
    switch (e) {
    case LoadStateError::Ok: return "Ok";
    case LoadStateError::BadMagic: return "BadMagic";
    case LoadStateError::IncompatibleFormat: return "IncompatibleFormat";
    case LoadStateError::IncompatibleCoreVersion: return "IncompatibleCoreVersion";
    case LoadStateError::WrongCartridge: return "WrongCartridge";
    case LoadStateError::ChecksumMismatch: return "ChecksumMismatch";
    }
    return "Unknown";
}

Json runStateResult(Session::Impl& st)
{
    return {{"runState", runStateName(st.machine.runState())},
        {"pausedByAutomation", st.pausedByAutomation}};
}

Json runStateSchema()
{
    return resultSchema({{"runState", s::stringEnum({"NeedsRom", "Paused", "Running"})},
                   {"pausedByAutomation", s::boolean()}},
        {"runState", "pausedByAutomation"});
}

// Advance commands pause autonomous pacing first (research R5).
void pauseForAutomation(Session::Impl& st)
{
    if (st.machine.runState() == RunState::Running) {
        st.machine.pause();
        st.pausedByAutomation = true;
    }
}

Json frameHashResult(Session::Impl& st)
{
    return {{"frameHash", media::hashHex(st.frame.hash())}};
}

Json hashSchema() { return s::pattern("^[0-9a-f]{8}$", "FNV-1a of the last frame"); }

std::uint8_t u8Arg(const Json& v) { return static_cast<std::uint8_t>(v.get<unsigned>()); }
std::uint16_t u16Arg(const Json& v) { return static_cast<std::uint16_t>(v.get<unsigned>()); }

// `length` bytes from `addr` must stay inside an address space of `size`.
std::optional<Result> checkRange(const Json& args, std::size_t size, std::size_t length)
{
    if (args["addr"].get<std::size_t>() + length > size) {
        return invalidArgument("/length", "runs past the end of the address space");
    }
    return std::nullopt;
}

bool blobOrBytes(Session::Impl& st, const Json& args, std::vector<std::uint8_t>& data,
    Result& failure)
{
    if (args.contains("bytes") == args.contains("blobId")) {
        failure = invalidArgument("", "give exactly one of bytes and blobId");
        return false;
    }
    if (args.contains("bytes")) {
        data = decodeBytes(args["bytes"]);
        return true;
    }
    const auto it = st.blobs.find(args["blobId"].get<std::string>());
    if (it == st.blobs.end() || !it->second.committed) {
        failure = Result::fail("UnknownHandle", "no committed blob " + args["blobId"].get<std::string>(),
            {{"blobId", args["blobId"]}});
        return false;
    }
    data = it->second.data;
    return true;
}

// Resolves a library-relative path, refusing anything that leaves the root
// (`..`, absolute paths, symlinks pointing outside).
std::optional<Result> resolveLibraryPath(Session::Impl& st, const std::string& rel,
    std::filesystem::path& resolved)
{
    namespace fs = std::filesystem;
    const auto outside = [&] {
        return Result::fail("PathOutsideLibrary", rel + " is outside the ROM library",
            {{"path", rel}});
    };
    if (st.config.libraryRoot.empty()) {
        return Result::fail("PathOutsideLibrary", "no ROM library is configured; upload instead",
            {{"path", rel}});
    }
    const fs::path relative(rel);
    if (relative.is_absolute() || relative.has_root_name() || relative.has_root_directory()) {
        return outside();
    }
    std::error_code ec;
    const fs::path root = fs::canonical(st.config.libraryRoot, ec);
    if (ec) {
        return Result::fail("NotFound", "the ROM library does not exist");
    }
    const fs::path target = fs::weakly_canonical(root / relative, ec);
    if (ec) {
        return outside();
    }
    const auto [r, t] = std::mismatch(root.begin(), root.end(), target.begin(), target.end());
    if (r != root.end()) {
        return outside();
    }
    if (!fs::is_regular_file(target, ec)) {
        return Result::fail("NotFound", rel + " is not a file in the ROM library",
            {{"path", rel}});
    }
    resolved = target;
    return std::nullopt;
}

void clearMedia(Session::Impl& st)
{
    st.frame.clear();
    st.audio.clear();
    st.pausedByAutomation = false;
}

void registerCartridge(Registry& r)
{
    addCommand(r, "core.load_rom",
        "Load an iNES/NES 2.0 ROM from the library (path) or an upload (blobId), then power on",
        s::object({{"path", s::string("library-relative path")},
            {"blobId", s::string("a committed upload")},
            {"powerOn", s::boolean("power on after loading (default true)")}}),
        resultSchema({{"cartridge", cartridgeSchema()}}, {"cartridge"}), true, false,
        [](CallContext& ctx, const Json& args) {
            auto& st = state(ctx);
            if (args.contains("path") == args.contains("blobId")) {
                return invalidArgument("", "give exactly one of path and blobId");
            }
            std::vector<std::uint8_t> rom;
            if (args.contains("path")) {
                std::filesystem::path file;
                if (auto failure = resolveLibraryPath(st, args["path"].get<std::string>(), file)) {
                    return *failure;
                }
                std::error_code ec;
                const auto size = std::filesystem::file_size(file, ec);
                if (ec || size > kMaxBlobSize) {
                    return Result::fail("LimitExceeded", "ROM file is too large",
                        {{"max", kMaxBlobSize}});
                }
                std::ifstream in(file, std::ios::binary);
                rom.assign(std::istreambuf_iterator<char>(in), {});
            } else {
                const auto it = st.blobs.find(args["blobId"].get<std::string>());
                if (it == st.blobs.end() || !it->second.committed) {
                    return Result::fail("UnknownHandle", "no committed blob "
                        + args["blobId"].get<std::string>(), {{"blobId", args["blobId"]}});
                }
                rom = it->second.data;
            }
            // A rejected load ejects the core's cartridge, so validate on a
            // scratch machine first: a failed load_rom changes nothing.
            {
                Machine scratch(st.config.machine);
                const LoadResult check = scratch.loadCartridge(rom.data(), rom.size());
                if (check.error != LoadError::Ok) {
                    return Result::fail("LoadError", "the ROM was rejected",
                        {{"error", loadErrorName(check.error)}, {"detail", check.detail}});
                }
            }
            const LoadResult loaded = st.machine.loadCartridge(rom.data(), rom.size());
            clearMedia(st);
            if (args.value("powerOn", true)) {
                st.machine.powerOn();
            }
            return Result::ok({{"cartridge", cartridgeJson(loaded.info)}});
        });

    addCommand(r, "core.eject", "Remove the cartridge", noArguments(), resultSchema(), true, false,
        [](CallContext& ctx, const Json&) {
            auto& st = state(ctx);
            st.machine.ejectCartridge();
            clearMedia(st);
            return Result::ok();
        });

    addCommand(r, "core.cartridge_info", "Header facts of the loaded cartridge", noArguments(),
        resultSchema({{"cartridge", cartridgeSchema()}}, {"cartridge"}), false, true,
        [](CallContext& ctx, const Json&) {
            return Result::ok({{"cartridge", cartridgeJson(state(ctx).machine.cartridgeInfo())}});
        });

    addCommand(r, "core.set_mmc3_revision", "Choose the MMC3 IRQ revision (auto follows the header)",
        s::object({{"revision", s::stringEnum({"auto", "A", "B"})}}, {"revision"}), resultSchema(),
        true, false, [](CallContext& ctx, const Json& args) {
            const std::string rev = args["revision"];
            state(ctx).machine.setMmc3Revision(rev == "A" ? Machine::Mmc3Revision::A
                    : rev == "B"                      ? Machine::Mmc3Revision::B
                                                      : Machine::Mmc3Revision::Auto);
            return Result::ok();
        });

    addCommand(r, "core.get_mmc3_revision", "The MMC3 IRQ revision setting", noArguments(),
        resultSchema({{"revision", s::stringEnum({"auto", "A", "B"})}}, {"revision"}), false, false,
        [](CallContext& ctx, const Json&) {
            const auto rev = state(ctx).machine.mmc3Revision();
            return Result::ok({{"revision", rev == Machine::Mmc3Revision::A ? "A"
                        : rev == Machine::Mmc3Revision::B                   ? "B"
                                                                            : "auto"}});
        });

    addCommand(r, "core.region", "The timing region of the loaded cartridge", noArguments(),
        resultSchema({{"region", s::stringEnum({"NTSC", "PAL", "Unspecified"})}}, {"region"}), false,
        false, [](CallContext& ctx, const Json&) {
            return Result::ok({{"region", regionName(state(ctx).machine.region())}});
        });
}

void registerRunControl(Registry& r)
{
    addCommand(r, "core.power_on", "Cold boot", noArguments(), resultSchema(), true, true,
        [](CallContext& ctx, const Json&) {
            state(ctx).machine.powerOn();
            return Result::ok();
        });

    addCommand(r, "core.reset", "Press the reset button", noArguments(), resultSchema(), true, true,
        [](CallContext& ctx, const Json&) {
            state(ctx).machine.reset();
            return Result::ok();
        });

    addCommand(r, "core.run_frames",
        "Run whole frames, optionally with per-frame controller 1 input (the last entry is held)",
        s::object({{"frames", s::integer(1, 216000)},
                       {"script", s::array(s::buttons(), 216000, "buttons per frame")}},
            {"frames"}),
        resultSchema({{"framesRun", s::u64()}, {"frameHash", hashSchema()}}, {"framesRun", "frameHash"}),
        true, true, [](CallContext& ctx, const Json& args) {
            auto& st = state(ctx);
            const auto frames = args["frames"].get<std::uint32_t>();
            if (frames > st.config.maxRunFrames) {
                return Result::fail("LimitExceeded", "too many frames in one call",
                    {{"max", st.config.maxRunFrames}});
            }
            std::vector<ButtonSet> script;
            for (const auto& entry : args.value("script", Json::array())) {
                script.push_back(parseButtons(entry));
            }
            pauseForAutomation(st);
            for (std::uint32_t i = 0; i < frames; ++i) {
                if (ctx.cancelled()) {
                    return Result::fail("Cancelled", "the run was cancelled at a frame boundary",
                        {{"framesRun", i}});
                }
                if (ctx.progress) {
                    ctx.progress(i, frames);
                }
                if (script.empty()) {
                    st.machine.frameAdvance();
                } else {
                    const InputScript one{&script[std::min<std::size_t>(i, script.size() - 1)], 1};
                    st.machine.runFrames(1, &one);
                }
            }
            Json v = frameHashResult(st);
            v["framesRun"] = frames;
            return Result::ok(v);
        });

    addCommand(r, "core.frame_advance", "Run exactly one frame", noArguments(),
        resultSchema({{"frameHash", hashSchema()}}, {"frameHash"}), true, true,
        [](CallContext& ctx, const Json&) {
            auto& st = state(ctx);
            pauseForAutomation(st);
            st.machine.frameAdvance();
            return Result::ok(frameHashResult(st));
        });

    addCommand(r, "core.step_instruction", "Execute CPU instructions",
        s::object({{"count", s::integer(1, 100000, "default 1")}}),
        resultSchema({{"cpu", cpuSchema()}}, {"cpu"}), true, true,
        [](CallContext& ctx, const Json& args) {
            auto& st = state(ctx);
            pauseForAutomation(st);
            const auto count = args.value("count", 1);
            for (int i = 0; i < count; ++i) {
                if (ctx.cancelled()) {
                    return Result::fail("Cancelled", "the step was cancelled",
                        {{"instructions", i}});
                }
                st.machine.stepInstruction();
            }
            return Result::ok({{"cpu", cpuJson(st.machine.cpu())}});
        });

    addCommand(r, "core.pause", "Pause autonomous pacing", noArguments(), runStateSchema(), true, false,
        [](CallContext& ctx, const Json&) {
            state(ctx).machine.pause();
            return Result::ok(runStateResult(state(ctx)));
        });

    addCommand(r, "core.resume", "Resume autonomous pacing", noArguments(), runStateSchema(), true, false,
        [](CallContext& ctx, const Json&) {
            auto& st = state(ctx);
            st.machine.resume();
            st.pausedByAutomation = false;
            return Result::ok(runStateResult(st));
        });

    addCommand(r, "core.run_state", "Run state and whether automation paused it", noArguments(),
        runStateSchema(), false, false,
        [](CallContext& ctx, const Json&) { return Result::ok(runStateResult(state(ctx))); });

    addCommand(r, "core.counters", "Frame counter and master cycle", noArguments(), resultSchema(), false, false,
        [](CallContext&, const Json&) { return Result::ok(); });
}

void registerInput(Registry& r)
{
    const Json port = s::integer(1, 2, "controller port");
    addCommand(r, "core.set_controller", "Replace the buttons held on a port",
        s::object({{"port", port}, {"buttons", s::buttons()}}, {"port", "buttons"}), resultSchema(),
        true, false, [](CallContext& ctx, const Json& args) {
            state(ctx).machine.setController(args["port"] == 1 ? Port::One : Port::Two,
                parseButtons(args["buttons"]));
            return Result::ok();
        });

    addCommand(r, "core.get_controller", "The buttons held on a port",
        s::object({{"port", port}}, {"port"}), resultSchema({{"buttons", s::buttons()}}, {"buttons"}),
        false, false, [](CallContext& ctx, const Json& args) {
            return Result::ok({{"buttons", buttonsJson(state(ctx).machine.controller(
                                               args["port"] == 1 ? Port::One : Port::Two))}});
        });
}

void registerMemory(Registry& r)
{
    const Json range = s::object({{"addr", s::u16()}, {"length", s::integer(1, 65536)}},
        {"addr", "length"});
    const Json bytesOut = resultSchema({{"bytes", s::bytes()}}, {"bytes"});

    addCommand(r, "core.read_memory", "Read the CPU bus with read side effects (e.g. $2002 clears VBlank)",
        range, bytesOut, true, false, [](CallContext& ctx, const Json& args) {
            const std::size_t len = args["length"];
            if (auto failure = checkRange(args, 0x10000, len)) return *failure;
            std::vector<std::uint8_t> v(len);
            state(ctx).machine.readMemory(u16Arg(args["addr"]), len, v.data());
            return Result::ok(bytesResult(v));
        });

    addCommand(r, "core.peek", "Read the CPU bus without side effects; registers ($2000-$5FFF) read as placeholders, see core.read_ppu_registers", range, bytesOut, false, false,
        [](CallContext& ctx, const Json& args) {
            const std::size_t len = args["length"];
            if (auto failure = checkRange(args, 0x10000, len)) return *failure;
            std::vector<std::uint8_t> v(len);
            for (std::size_t i = 0; i < len; ++i) {
                v[i] = state(ctx).machine.peek(static_cast<std::uint16_t>(args["addr"].get<std::size_t>() + i));
            }
            return Result::ok(bytesResult(v));
        });

    addCommand(r, "core.write_memory", "Write bytes to the CPU bus",
        s::object({{"addr", s::u16()}, {"bytes", s::bytes()}}, {"addr", "bytes"}), resultSchema(), true,
        false, [](CallContext& ctx, const Json& args) {
            const auto data = decodeBytes(args["bytes"]);
            if (auto failure = checkRange(args, 0x10000, data.size())) return *failure;
            const WriteError e = state(ctx).machine.writeMemory(u16Arg(args["addr"]), data.data(), data.size());
            if (e != WriteError::Ok) {
                return Result::fail("WriteError", "the write was refused", {{"error", "InvalidAddress"}});
            }
            return Result::ok();
        });

    addCommand(r, "core.write_cpu_register", "Write one PPU/APU/IO register ($2000-$401F)",
        s::object({{"addr", s::u16()}, {"value", s::u8()}}, {"addr", "value"}), resultSchema(), true,
        false, [](CallContext& ctx, const Json& args) {
            const WriteError e = state(ctx).machine.writeCpuRegister(u16Arg(args["addr"]), u8Arg(args["value"]));
            if (e != WriteError::Ok) {
                return Result::fail("WriteError", "not a CPU register address",
                    {{"error", "InvalidAddress"}});
            }
            return Result::ok();
        });

    addCommand(r, "core.get_cpu", "CPU registers", noArguments(), resultSchema({{"cpu", cpuSchema()}}, {"cpu"}), false,
        false, [](CallContext& ctx, const Json&) {
            return Result::ok({{"cpu", cpuJson(state(ctx).machine.cpu())}});
        });

    addCommand(r, "core.set_cpu", "Write any of the CPU registers (P bit 5 stays set)",
        s::object({{"a", s::u8()}, {"x", s::u8()}, {"y", s::u8()}, {"s", s::u8()},
            {"p", s::u8()}, {"pc", s::u16()}}),
        resultSchema({{"cpu", cpuSchema()}}, {"cpu"}), true, false,
        [](CallContext& ctx, const Json& args) {
            CpuWrite w;
            const auto field = [&](const char* name, int bit, auto& target) {
                if (args.contains(name)) {
                    target = static_cast<std::remove_reference_t<decltype(target)>>(args[name].get<unsigned>());
                    w.mask = static_cast<std::uint8_t>(w.mask | (1u << bit));
                }
            };
            field("a", 0, w.a);
            field("x", 1, w.x);
            field("y", 2, w.y);
            field("s", 3, w.s);
            field("p", 4, w.p);
            field("pc", 5, w.pc);
            state(ctx).machine.writeCpu(w);
            return Result::ok({{"cpu", cpuJson(state(ctx).machine.cpu())}});
        });

    addCommand(r, "core.disassemble", "Disassemble forward from an address without executing",
        s::object({{"addr", s::u16()}, {"count", s::integer(1, 256)}}, {"addr", "count"}),
        resultSchema({{"lines", s::array(s::object({{"addr", s::u16()}, {"text", s::string()}},
                                    {"addr", "text"}), 256)}},
            {"lines"}),
        false, false, [](CallContext& ctx, const Json& args) {
            Json lines = Json::array();
            auto pc = u16Arg(args["addr"]);
            for (int i = 0; i < args["count"].get<int>(); ++i) {
                const std::string text = state(ctx).machine.disassemble(pc);
                lines.push_back({{"addr", pc}, {"text", text}});
                // Trace format: "C000  4C F5 C5  JMP ..." lists 1-3 bytes.
                int length = 0;
                for (std::size_t k = 6; k + 1 < 15 && k + 1 < text.size(); k += 3) {
                    if (text[k] != ' ') ++length;
                }
                pc = static_cast<std::uint16_t>(pc + std::max(length, 1));
            }
            return Result::ok({{"lines", lines}});
        });

    addCommand(r, "core.trace", "Step instructions and return each one's registers and PPU position",
        s::object({{"instructions", s::integer(1, 256)}}, {"instructions"}),
        resultSchema({{"lines", s::array(s::object({{"cpu", cpuSchema()}, {"scanline", s::u64()},
                                    {"dot", s::u64()}, {"text", s::string()}},
                                    {"cpu", "scanline", "dot", "text"}), 256)}},
            {"lines"}),
        true, true, [](CallContext& ctx, const Json& args) {
            auto& st = state(ctx);
            pauseForAutomation(st);
            Json lines = Json::array();
            st.machine.setInstructionTrace([&](const TracePoint& tp) {
                lines.push_back({{"cpu", cpuJson(tp.cpu)}, {"scanline", tp.scanline},
                    {"dot", tp.dot}, {"text", st.machine.disassemble(tp.cpu.pc)}});
            });
            for (int i = 0; i < args["instructions"].get<int>(); ++i) {
                st.machine.stepInstruction();
            }
            st.machine.setInstructionTrace(nullptr);
            return Result::ok({{"lines", lines}});
        });
}

void registerPpu(Registry& r)
{
    addCommand(r, "core.read_ppu_registers", "The eight PPU registers, without latch side effects",
        noArguments(), resultSchema({{"bytes", s::bytes(8)}}, {"bytes"}), false, false,
        [](CallContext& ctx, const Json&) {
            std::vector<std::uint8_t> v(8);
            state(ctx).machine.readPpuRegisters(v.data());
            return Result::ok(bytesResult(v));
        });

    addCommand(r, "core.read_vram", "Read the PPU bus ($0000-$3FFF, mirroring resolved)",
        s::object({{"addr", s::integer(0, 0x3FFF)}, {"length", s::integer(1, 0x4000)}},
            {"addr", "length"}),
        resultSchema({{"bytes", s::bytes(0x4000)}}, {"bytes"}), false, false,
        [](CallContext& ctx, const Json& args) {
            const std::size_t len = args["length"];
            if (auto failure = checkRange(args, 0x4000, len)) return *failure;
            std::vector<std::uint8_t> v(len);
            state(ctx).machine.readVram(u16Arg(args["addr"]), len, v.data());
            return Result::ok(bytesResult(v));
        });

    addCommand(r, "core.write_vram", "Write the PPU bus",
        s::object({{"addr", s::integer(0, 0x3FFF)}, {"bytes", s::bytes(0x4000)}},
            {"addr", "bytes"}),
        resultSchema(), true, false, [](CallContext& ctx, const Json& args) {
            const auto data = decodeBytes(args["bytes"]);
            if (auto failure = checkRange(args, 0x4000, data.size())) return *failure;
            state(ctx).machine.writeVram(u16Arg(args["addr"]), data.data(), data.size());
            return Result::ok();
        });

    const auto fixedBlock = [&r](const char* name, std::size_t size, const char* what,
                                auto read, auto write) {
        addCommand(r, std::string("core.read_") + name, std::string("Read ") + what, noArguments(),
            resultSchema({{"bytes", s::bytes(size)}}, {"bytes"}), false, false,
            [size, read](CallContext& ctx, const Json&) {
                std::vector<std::uint8_t> v(size);
                read(state(ctx).machine, v.data());
                return Result::ok(bytesResult(v));
            });
        addCommand(r, std::string("core.write_") + name, std::string("Replace ") + what,
            s::object({{"bytes", s::bytes(size)}}, {"bytes"}), resultSchema(), true, false,
            [size, write](CallContext& ctx, const Json& args) {
                const auto data = decodeBytes(args["bytes"]);
                if (data.size() != size) {
                    return invalidArgument("/bytes", "must be exactly " + std::to_string(size) + " bytes");
                }
                write(state(ctx).machine, data.data());
                return Result::ok();
            });
    };
    fixedBlock("oam", 256, "sprite memory (256 bytes)",
        [](Machine& m, std::uint8_t* d) { m.readOam(d); },
        [](Machine& m, const std::uint8_t* d) { m.writeOam(d); });
    fixedBlock("palette", 32, "palette RAM ($3F00-$3F1F)",
        [](Machine& m, std::uint8_t* d) { m.readPalette(d); },
        [](Machine& m, const std::uint8_t* d) { m.writePalette(d); });
}

void registerStates(Registry& r)
{
    const Json blobOut = resultSchema({{"blobId", s::string()}, {"size", s::u64()}}, {"blobId", "size"});

    addCommand(r, "core.capture_state", "Capture a save state into a blob (read it with session.blob_read)",
        noArguments(), blobOut, true, true, [](CallContext& ctx, const Json&) {
            auto& st = state(ctx);
            const SaveStateBlob blob = st.machine.captureState();
            std::vector<std::uint8_t> bytes(blob.data, blob.data + blob.size);
            const auto size = bytes.size();
            return Result::ok({{"blobId", st.addBlob(std::move(bytes))}, {"size", size}});
        });

    addCommand(r, "core.load_state", "Load a save state from a blob or inline bytes",
        s::object({{"blobId", s::string()}, {"bytes", s::bytes()}}), resultSchema(), true, true,
        [](CallContext& ctx, const Json& args) {
            auto& st = state(ctx);
            std::vector<std::uint8_t> data;
            Result failure;
            if (!blobOrBytes(st, args, data, failure)) {
                return failure;
            }
            const LoadStateError e = st.machine.loadState(data.data(), data.size());
            if (e != LoadStateError::Ok) {
                return Result::fail("LoadStateError", "the state was rejected",
                    {{"error", loadStateErrorName(e)}});
            }
            return Result::ok();
        });

    addCommand(r, "core.read_sram", "Read battery-backed cartridge RAM",
        s::object({{"offset", s::integer(0, kMaxBlobSize)}, {"length", s::integer(1, 65536)}},
            {"offset", "length"}),
        resultSchema({{"bytes", s::bytes()}, {"size", s::u64()}}, {"bytes", "size"}), false, true,
        [](CallContext& ctx, const Json& args) {
            const SramRange sram = state(ctx).machine.sram(); // empty without a battery
            if (sram.size == 0) {
                return Result::fail("NoBattery", "the cartridge has no battery-backed RAM");
            }
            const std::size_t offset = args["offset"];
            const std::size_t len = args["length"];
            if (offset + len > sram.size) {
                return invalidArgument("/length", "runs past the end of SRAM ("
                    + std::to_string(sram.size) + " bytes)");
            }
            std::vector<std::uint8_t> v(sram.data + offset, sram.data + offset + len);
            Json res = bytesResult(v);
            res["size"] = sram.size;
            return Result::ok(res);
        });

    addCommand(r, "core.load_sram", "Replace battery-backed RAM (padded or trimmed to its size)",
        s::object({{"bytes", s::bytes()}}, {"bytes"}), resultSchema(), true, true,
        [](CallContext& ctx, const Json& args) {
            auto& m = state(ctx).machine;
            if (m.sram().size == 0) {
                return Result::fail("NoBattery", "the cartridge has no battery-backed RAM");
            }
            const auto data = decodeBytes(args["bytes"]);
            m.loadSram(data.data(), data.size());
            return Result::ok();
        });

    addCommand(r, "core.dump_memory", "The memory dump bytes (the format dumpMemory writes) as a blob",
        noArguments(), blobOut, false, false, [](CallContext& ctx, const Json&) {
            auto& st = state(ctx);
            auto bytes = st.machine.memoryDump(0);
            const auto size = bytes.size();
            return Result::ok({{"blobId", st.addBlob(std::move(bytes))}, {"size", size}});
        });
}

void registerMedia(Registry& r)
{
    addCommand(r, "core.screen", "The last frame: a 256x240 PNG inline, or raw XRGB8888 as a blob",
        s::object({{"format", s::stringEnum({"png", "raw"}, "default png")}}),
        resultSchema({{"format", s::stringEnum({"png", "raw"})}, {"png", s::string("base64 PNG")},
                {"blobId", s::string()}, {"size", s::u64()}, {"width", s::u64()},
                {"height", s::u64()}, {"frameIndex", s::u64()}, {"hasFrame", s::boolean()},
                {"frameHash", hashSchema()}},
            {"format", "width", "height", "frameIndex", "hasFrame", "frameHash"}),
        false, false, [](CallContext& ctx, const Json& args) {
            auto& st = state(ctx);
            const std::string format = args.value("format", "png");
            Json v = frameHashResult(st);
            v["format"] = format;
            v["width"] = media::kFrameWidth;
            v["height"] = media::kFrameHeight;
            v["frameIndex"] = st.frame.frameIndex();
            v["hasFrame"] = st.frame.hasFrame();
            if (format == "png") {
                v["png"] = media::base64Encode(media::encodePng(st.frame.pixels().data()));
            } else {
                auto bytes = st.frame.bytes();
                v["size"] = bytes.size();
                v["blobId"] = st.addBlob(std::move(bytes));
            }
            return Result::ok(v);
        });

    addCommand(r, "core.frame_hash", "FNV-1a of the last frame (the playtest harness hash)", noArguments(),
        resultSchema({{"frameHash", hashSchema()}, {"hasFrame", s::boolean()}}, {"frameHash", "hasFrame"}),
        false, false, [](CallContext& ctx, const Json&) {
            Json v = frameHashResult(state(ctx));
            v["hasFrame"] = state(ctx).frame.hasFrame();
            return Result::ok(v);
        });

    addCommand(r, "core.audio",
        "Recent audio (10 s ring): s16le mono chunks starting at or after sinceMasterCycle",
        s::object({{"sinceMasterCycle", s::u64("default 0")},
            {"maxSamples", s::integer(1, 32768, "default 32768")}}),
        resultSchema({{"pcm", s::bytes()}, {"samples", s::u64()}, {"sampleRate", s::u64()},
                {"startMasterCycle", s::u64()}, {"next", s::u64("sinceMasterCycle for the next call")},
                {"truncated", s::boolean()}, {"dropped", s::boolean()}},
            {"pcm", "samples", "sampleRate", "startMasterCycle", "next", "truncated", "dropped"}),
        false, false, [](CallContext& ctx, const Json& args) {
            auto& st = state(ctx);
            const auto read = st.audio.read(args.value("sinceMasterCycle", std::uint64_t{0}),
                args.value("maxSamples", std::size_t{32768}));
            std::vector<std::uint8_t> bytes;
            bytes.reserve(read.pcm.size() * 2);
            for (const std::int16_t sample : read.pcm) {
                const auto u = static_cast<std::uint16_t>(sample);
                bytes.push_back(static_cast<std::uint8_t>(u));
                bytes.push_back(static_cast<std::uint8_t>(u >> 8));
            }
            return Result::ok({{"pcm", media::base64Encode(bytes)}, {"samples", read.pcm.size()},
                {"sampleRate", st.audio.sampleRate()}, {"startMasterCycle", read.startMasterCycle},
                {"next", read.next}, {"truncated", read.truncated}, {"dropped", read.dropped}});
        });
}

} // namespace

Json resultSchema(std::initializer_list<std::pair<const char*, Json>> properties,
    std::vector<std::string> required)
{
    // Registry::add appends frame and masterCycle.
    return s::object(properties, std::move(required));
}

Json noArguments() { return s::object({}); }

void addCommand(Registry& registry, std::string name, std::string description, Json input,
    Json output, bool mutating, bool needsRom,
    std::function<Result(CallContext&, const Json&)> handler)
{
    Command c;
    c.name = std::move(name);
    c.description = std::move(description);
    c.inputSchema = std::move(input);
    c.outputSchema = std::move(output);
    c.mutating = mutating;
    c.needsRom = needsRom;
    c.handler = std::move(handler);
    registry.add(std::move(c));
}

Result invalidArgument(const std::string& path, const std::string& message)
{
    return Result::fail("InvalidArguments", path + " " + message, {{"path", path}});
}

const char* stateErrorName(LoadStateError e) { return loadStateErrorName(e); }

std::vector<std::uint8_t> decodeBytes(const Json& value)
{
    return media::base64Decode(value.get<std::string>()).value_or(std::vector<std::uint8_t>{});
}

const char* runStateName(RunState state)
{
    switch (state) {
    case RunState::NeedsRom: return "NeedsRom";
    case RunState::Paused: return "Paused";
    case RunState::Running: return "Running";
    }
    return "NeedsRom";
}

Json cartridgeJson(const CartridgeInfo& info)
{
    const char* mirroring = "Horizontal";
    switch (info.mirroring) {
    case Mirroring::Horizontal: mirroring = "Horizontal"; break;
    case Mirroring::Vertical: mirroring = "Vertical"; break;
    case Mirroring::FourScreen: mirroring = "FourScreen"; break;
    case Mirroring::MapperControlled: mirroring = "MapperControlled"; break;
    }
    return {{"format", info.formatVersion == 2 ? "NES 2.0" : "iNES"},
        {"mapper", info.mapperId}, {"submapper", info.submapper},
        {"mirroring", mirroring}, {"battery", info.battery}, {"prgSize", info.prgSize},
        {"chrSize", info.chrSize}, {"prgRamSize", info.prgRamSize},
        {"region", regionName(info.region)}};
}

void registerCoreCommands(Registry& registry)
{
    registerCartridge(registry);
    registerRunControl(registry);
    registerInput(registry);
    registerMemory(registry);
    registerPpu(registry);
    registerStates(registry);
    registerMedia(registry);
}

} // namespace cutenes::api
