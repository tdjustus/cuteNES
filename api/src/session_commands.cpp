// session.* and api.*: consumer-side services and surface metadata
// (command-catalog.md).

#include "session_impl.hpp"

#include <cutenes/version.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace cutenes::api {

namespace s = schema;

namespace {

constexpr std::size_t kMaxLibraryEntries = 1000;
const char* const kSlotPattern = "^[A-Za-z0-9_-]{1,64}$";

Result unknownBlob(const std::string& id)
{
    return Result::fail("UnknownHandle", "no blob " + id, {{"blobId", id}});
}

void registerBlobs(Registry& r)
{
    const Json id = s::string("a blob handle");

    addCommand(r, "session.blob_begin",
        "Start an upload (a ROM or a save state); send it with blob_chunk, then blob_commit",
        s::object({{"size", s::integer(1, kMaxBlobSize, "total bytes")}}, {"size"}),
        resultSchema({{"blobId", s::string()}}, {"blobId"}), true, false,
        [](CallContext& ctx, const Json& args) {
            auto& st = state(ctx);
            const std::size_t size = args["size"];
            st.evictFor(size);
            if (st.blobBytes() + size > kMaxBlobTotal) {
                return Result::fail("LimitExceeded", "too many pending uploads",
                    {{"max", kMaxBlobTotal}});
            }
            Blob b;
            b.expectedSize = size;
            b.deadline = std::chrono::steady_clock::now() + kUploadTimeout;
            b.order = st.nextBlob;
            const std::string blobId = st.newBlobId();
            st.blobs.emplace(blobId, std::move(b));
            return Result::ok({{"blobId", blobId}});
        });

    addCommand(r, "session.blob_chunk",
        "Append up to 64 KiB to an upload; offset must equal the bytes received so far",
        s::object({{"blobId", id}, {"offset", s::integer(0, kMaxBlobSize)}, {"bytes", s::bytes()}},
            {"blobId", "offset", "bytes"}),
        resultSchema({{"received", s::u64()}}, {"received"}), true, false,
        [](CallContext& ctx, const Json& args) {
            auto& st = state(ctx);
            const auto it = st.blobs.find(args["blobId"].get<std::string>());
            if (it == st.blobs.end() || it->second.committed) {
                return unknownBlob(args["blobId"]);
            }
            Blob& b = it->second;
            if (args["offset"].get<std::size_t>() != b.data.size()) {
                return invalidArgument("/offset", "must be " + std::to_string(b.data.size()));
            }
            const auto data = decodeBytes(args["bytes"]);
            if (b.data.size() + data.size() > b.expectedSize) {
                return Result::fail("LimitExceeded", "more bytes than blob_begin declared",
                    {{"max", b.expectedSize}});
            }
            b.data.insert(b.data.end(), data.begin(), data.end());
            b.deadline = std::chrono::steady_clock::now() + kUploadTimeout;
            return Result::ok({{"received", b.data.size()}});
        });

    addCommand(r, "session.blob_commit", "Finish an upload; only committed blobs can be used",
        s::object({{"blobId", id}}, {"blobId"}), resultSchema({{"size", s::u64()}}, {"size"}),
        true, false, [](CallContext& ctx, const Json& args) {
            auto& st = state(ctx);
            const auto it = st.blobs.find(args["blobId"].get<std::string>());
            if (it == st.blobs.end() || it->second.committed) {
                return unknownBlob(args["blobId"]);
            }
            if (it->second.data.size() != it->second.expectedSize) {
                return invalidArgument("/blobId", "is incomplete: "
                    + std::to_string(it->second.data.size()) + " of "
                    + std::to_string(it->second.expectedSize) + " bytes");
            }
            it->second.committed = true;
            return Result::ok({{"size", it->second.data.size()}});
        });

    addCommand(r, "session.blob_read", "Read up to 64 KiB of a committed blob",
        s::object({{"blobId", id}, {"offset", s::integer(0, kMaxBlobSize)},
                      {"length", s::integer(1, 65536)}},
            {"blobId", "offset", "length"}),
        resultSchema({{"bytes", s::bytes()}, {"size", s::u64()}, {"eof", s::boolean()}},
            {"bytes", "size", "eof"}),
        false, false, [](CallContext& ctx, const Json& args) {
            auto& st = state(ctx);
            const auto it = st.blobs.find(args["blobId"].get<std::string>());
            if (it == st.blobs.end() || !it->second.committed) {
                return unknownBlob(args["blobId"]);
            }
            const auto& data = it->second.data;
            const std::size_t offset = std::min(args["offset"].get<std::size_t>(), data.size());
            const std::size_t end = std::min(data.size(), offset + args["length"].get<std::size_t>());
            const auto first = data.begin() + static_cast<std::ptrdiff_t>(offset);
            const auto last = data.begin() + static_cast<std::ptrdiff_t>(end);
            return Result::ok({{"bytes", media::base64Encode(std::vector<std::uint8_t>(first, last))},
                {"size", data.size()}, {"eof", end == data.size()}});
        });

    addCommand(r, "session.blob_delete", "Discard a blob or abandon an upload",
        s::object({{"blobId", id}}, {"blobId"}), resultSchema(), true, false,
        [](CallContext& ctx, const Json& args) {
            if (state(ctx).blobs.erase(args["blobId"].get<std::string>()) == 0) {
                return unknownBlob(args["blobId"]);
            }
            return Result::ok();
        });
}

void registerSlots(Registry& r)
{
    const Json name = s::pattern(kSlotPattern, "slot name");

    addCommand(r, "session.save_slot", "Save the state into a named slot (overwrites)",
        s::object({{"name", name}}, {"name"}), resultSchema({{"size", s::u64()}}, {"size"}), true,
        true, [](CallContext& ctx, const Json& args) {
            auto& st = state(ctx);
            const SaveStateBlob blob = st.machine.captureState();
            Slot slot{{blob.data, blob.data + blob.size}, st.machine.frameCounter(),
                st.machine.masterCycle()};
            const auto size = slot.state.size();
            st.slots[args["name"].get<std::string>()] = std::move(slot);
            return Result::ok({{"size", size}});
        });

    addCommand(r, "session.load_slot", "Load a named slot",
        s::object({{"name", name}}, {"name"}), resultSchema(), true, true,
        [](CallContext& ctx, const Json& args) {
            auto& st = state(ctx);
            const auto it = st.slots.find(args["name"].get<std::string>());
            if (it == st.slots.end()) {
                return Result::fail("UnknownHandle", "no slot " + args["name"].get<std::string>(),
                    {{"slot", args["name"]}});
            }
            const LoadStateError e =
                st.machine.loadState(it->second.state.data(), it->second.state.size());
            if (e != LoadStateError::Ok) {
                return Result::fail("LoadStateError", "the slot was rejected",
                    {{"error", stateErrorName(e)}});
            }
            return Result::ok();
        });

    addCommand(r, "session.list_slots", "Saved slots, by name", noArguments(),
        resultSchema({{"slots", s::array(s::object({{"name", s::string()}, {"size", s::u64()},
                                                {"frame", s::u64()}, {"masterCycle", s::u64()}},
                                                {"name", "size", "frame", "masterCycle"}),
                                     100000)}},
            {"slots"}),
        false, false, [](CallContext& ctx, const Json&) {
            Json slots = Json::array();
            for (const auto& [slotName, slot] : state(ctx).slots) {
                slots.push_back({{"name", slotName}, {"size", slot.state.size()},
                    {"frame", slot.frame}, {"masterCycle", slot.masterCycle}});
            }
            return Result::ok({{"slots", slots}});
        });

    addCommand(r, "session.delete_slot", "Delete a named slot",
        s::object({{"name", name}}, {"name"}), resultSchema(), true, false,
        [](CallContext& ctx, const Json& args) {
            if (state(ctx).slots.erase(args["name"].get<std::string>()) == 0) {
                return Result::fail("UnknownHandle", "no slot " + args["name"].get<std::string>(),
                    {{"slot", args["name"]}});
            }
            return Result::ok();
        });
}

Json statusJson(Session& session)
{
    auto& st = session.impl();
    Json v = {{"runState", runStateName(st.machine.runState())},
        {"pausedByAutomation", st.pausedByAutomation}, {"mode", session.mode()},
        {"coreVersion", coreVersionString()}, {"surfaceVersion", surfaceVersion()},
        {"cartridgeLoaded", st.machine.cartridgeInfo().prgSize > 0},
        {"libraryConfigured", !st.config.libraryRoot.empty()}};
    if (st.machine.cartridgeInfo().prgSize > 0) {
        v["cartridge"] = cartridgeJson(st.machine.cartridgeInfo());
    }
    return v;
}

void registerStatus(Registry& r)
{
    addCommand(r, "session.status",
        "Cartridge, run state, mode and versions (FR-018)", noArguments(),
        resultSchema({{"runState", s::stringEnum({"NeedsRom", "Paused", "Running"})},
                         {"pausedByAutomation", s::boolean()},
                         {"mode", s::stringEnum({"headless", "gui"})},
                         {"coreVersion", s::string()}, {"surfaceVersion", s::string()},
                         {"cartridgeLoaded", s::boolean()}, {"libraryConfigured", s::boolean()},
                         {"cartridge", {{"type", "object"}}}},
            {"runState", "pausedByAutomation", "mode", "coreVersion", "surfaceVersion",
                "cartridgeLoaded", "libraryConfigured"}),
        false, false,
        [](CallContext& ctx, const Json&) { return Result::ok(statusJson(ctx.session)); });

    addCommand(r, "session.library_list", "ROM files under the library root (relative paths)",
        noArguments(),
        resultSchema({{"roms", s::array(s::string(), kMaxLibraryEntries)},
                         {"truncated", s::boolean()}},
            {"roms", "truncated"}),
        false, false, [](CallContext& ctx, const Json&) {
            namespace fs = std::filesystem;
            auto& st = state(ctx);
            Json roms = Json::array();
            bool truncated = false;
            std::error_code ec;
            const fs::path root = st.config.libraryRoot.empty()
                ? fs::path()
                : fs::canonical(st.config.libraryRoot, ec);
            if (!root.empty() && !ec) {
                std::vector<std::string> found;
                for (auto it = fs::recursive_directory_iterator(root,
                         fs::directory_options::skip_permission_denied, ec);
                     !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
                    std::string ext = it->path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(),
                        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    // Symlinks are listed only when they resolve inside the root.
                    const fs::path real = fs::weakly_canonical(it->path(), ec);
                    if (ec || ext != ".nes" || !fs::is_regular_file(real, ec)) {
                        ec.clear();
                        continue;
                    }
                    const auto [rootEnd, realPos] =
                        std::mismatch(root.begin(), root.end(), real.begin(), real.end());
                    if (rootEnd == root.end()) {
                        found.push_back(fs::relative(it->path(), root).generic_string());
                    }
                }
                std::sort(found.begin(), found.end());
                truncated = found.size() > kMaxLibraryEntries;
                found.resize(std::min(found.size(), kMaxLibraryEntries));
                roms = found;
            }
            return Result::ok({{"roms", roms}, {"truncated", truncated}});
        });

    addCommand(r, "session.write_dump",
        "Write <datetime>.dmp (Machine::dumpMemory) into the server's debug directory",
        noArguments(), resultSchema({{"file", s::string()}}, {"file"}), false, false,
        [](CallContext& ctx, const Json&) {
            auto& st = state(ctx);
            const std::string path = st.machine.dumpMemory(st.config.debugDir);
            if (path.empty()) {
                return Result::fail("WriteError", "the dump could not be written",
                    {{"error", "IoError"}});
            }
            return Result::ok({{"file", std::filesystem::path(path).filename().string()}});
        });
}

void registerApi(Registry& r)
{
    addCommand(r, "api.version", "Surface version (MAJOR.MINOR), core version and mode",
        noArguments(),
        resultSchema({{"surface", s::string()}, {"core", s::string()},
                         {"mode", s::stringEnum({"headless", "gui"})}},
            {"surface", "core", "mode"}),
        false, false, [](CallContext& ctx, const Json&) {
            return Result::ok({{"surface", surfaceVersion()}, {"core", coreVersionString()},
                {"mode", ctx.session.mode()}});
        });

    addCommand(r, "api.catalog", "Every command in this mode with its input and output schemas",
        noArguments(),
        resultSchema({{"surface", s::string()},
                         {"commands", s::array({{"type", "object"}}, 100000)}},
            {"surface", "commands"}),
        false, false, [](CallContext& ctx, const Json&) {
            return Result::ok({{"surface", surfaceVersion()},
                {"commands", ctx.session.registry().catalog()}});
        });
}

} // namespace

void registerSessionCommands(Registry& registry)
{
    registerBlobs(registry);
    registerSlots(registry);
    registerStatus(registry);
    registerApi(registry);
}

} // namespace cutenes::api
