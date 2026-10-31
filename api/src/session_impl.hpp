#pragma once

// Session state shared by the dispatcher and the command handlers. Only the
// emulation thread reads or writes it, except where a member says otherwise.

#include <cutenes/api/schema.hpp>
#include <cutenes/api/session.hpp>

#include "media.hpp"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace cutenes::api {

// Blob handles (command-catalog.md "Blobs"): uploads, captured states,
// memory dumps and raw screens all live here and are read in chunks.
inline constexpr std::size_t kMaxBlobSize = 16 * 1024 * 1024;
inline constexpr std::size_t kMaxBlobTotal = 64 * 1024 * 1024;
inline constexpr std::chrono::seconds kUploadTimeout{60};

struct Blob {
    std::vector<std::uint8_t> data;
    std::size_t expectedSize = 0;
    bool committed = false;
    std::chrono::steady_clock::time_point deadline{}; // pending uploads only
    std::uint64_t order = 0; // creation order, for evicting the oldest
};

struct Slot {
    std::vector<std::uint8_t> state;
    std::uint64_t frame = 0;
    std::uint64_t masterCycle = 0;
};

struct Session::Impl {
    explicit Impl(SessionConfig c);

    SessionConfig config;
    HeadlessHost headlessHost;
    IHost* host = nullptr;

    Machine machine;
    Registry registry;
    media::FrameCapture frame;
    media::AudioRing audio;
    bool pausedByAutomation = false;

    std::map<std::string, Blob> blobs;
    std::uint64_t nextBlob = 1;
    std::map<std::string, Slot> slots;

    // A committed blob holding `data`; evicts the oldest committed blobs
    // when the total would exceed kMaxBlobTotal.
    std::string addBlob(std::vector<std::uint8_t> data);
    std::string newBlobId();
    std::size_t blobBytes() const;
    void evictFor(std::size_t incoming);
    void expireUploads();

    // The command queue (research R5).
    struct Job {
        std::function<Result()> run;
        std::promise<Result> done;
    };
    std::mutex queueMutex;
    std::condition_variable queueCv;
    std::deque<Job> queue;
    bool stopping = false; // guarded by queueMutex
    std::atomic<bool> stopFlag{false};
    std::thread thread;
    std::thread::id threadId;

    void loop();
    std::future<Result> post(std::function<Result()> run);

    // Listeners may be added from any thread.
    std::mutex listenerMutex;
    std::map<int, std::function<void(const Event&)>> listeners;
    int nextListener = 1;
    void emit(const std::string& type, Json data = Json::object());
    std::chrono::steady_clock::time_point lastFrameEvent{};
    void emitFrame();

    Result dispatch(Session& session, const std::string& name, const Json& args,
        const CancelFlag& cancel);
};

void registerCoreCommands(Registry& registry);
void registerSessionCommands(Registry& registry);

// Shared helpers for the handlers.
inline Session::Impl& state(CallContext& ctx) { return ctx.session.impl(); }

// A result schema of the command's own fields (the registry adds the
// frame/masterCycle pair every result carries).
Json resultSchema(std::initializer_list<std::pair<const char*, Json>> properties = {},
    std::vector<std::string> required = {});
Json noArguments();
void addCommand(Registry& registry, std::string name, std::string description, Json input,
    Json output, bool mutating, bool needsRom,
    std::function<Result(CallContext&, const Json&)> handler);
Result invalidArgument(const std::string& path, const std::string& message);

std::vector<std::uint8_t> decodeBytes(const Json& value); // schema-validated
Json cartridgeJson(const CartridgeInfo& info);
const char* runStateName(RunState state);
const char* stateErrorName(LoadStateError error);

} // namespace cutenes::api
